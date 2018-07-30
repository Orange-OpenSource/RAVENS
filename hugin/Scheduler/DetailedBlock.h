//
// Created by Emile-Hugo Spir on 3/15/18.
//

#ifndef HERMES_DETAILEDBLOCK_H
#define HERMES_DETAILEDBLOCK_H

//Source is where the data we need currently is
//Destination is where the data belong

#include <unordered_set>
#include <algorithm>
#include <functional>

struct DetailedBlockMetadata
{
	Address source;
	Address destination;
	bool tagged;
	bool willUntag;

	size_t length;

	DetailedBlockMetadata(const BlockID & blockID, bool shouldTag = false) : source(blockID, 0), destination(blockID, 0), length(BLOCK_SIZE), tagged(shouldTag), willUntag(false) {}
	DetailedBlockMetadata(const Address & address, const size_t length = BLOCK_SIZE, bool shouldTag = false) : source(address), destination(address), length(length), tagged(shouldTag), willUntag(false) {}
	DetailedBlockMetadata(const Address & source, const Address & destination, const size_t length = BLOCK_SIZE, bool shouldTag = false) : source(source), destination(destination), length(length), tagged(shouldTag), willUntag(false) {}
	DetailedBlockMetadata(const Token & token, bool shouldTag = false) : source(token.origin), destination(token.finalAddress), length(token.length), tagged(shouldTag), willUntag(false) {}

	bool fitWithinDestination(const Address & needle) const
	{
		return fitWithin(destination, length, needle);
	}

	static bool fitWithin(const Address & hayStack, const size_t & length, const Address & needle)
	{
		return (hayStack.value <= needle.value && hayStack.value + length > needle.value);
	}

	bool overlapWith(const Address & needle, size_t needleLength) const
	{
		return overlapWith(needle, needleLength, source, length);
	}

	static bool overlapWith(const Address & needle, size_t needleLength, const Address & hayStack, size_t hayStackLength)
	{
		if(needleLength == 0 || hayStackLength == 0)
			return false;

		if(fitWithin(hayStack, hayStackLength, needle))
			return true;

		if(fitWithin(hayStack, hayStackLength, needle + (needleLength - 1)))
			return true;

		return (needle.value <= hayStack.value) ==
				(needle.value + needleLength > hayStack.value + hayStackLength);
	}

	size_t getFinalDestOffset() const
	{
		return destination.getOffset() + length;
	}

	bool operator < (const DetailedBlockMetadata & b) const	{	return source <  b.source;		}
	bool operator ==(const DetailedBlockMetadata & b) const	{	return source ==  b.source && destination == b.destination && length == b.length;	}
};

struct DetailedBlock
{
	bool sorted;
	vector<DetailedBlockMetadata> segments;

	DetailedBlock() : sorted(true)
	{
		segments.reserve(0x20);
	}

	explicit DetailedBlock(const BlockID &blockID) : segments({blockID}), sorted(true)
	{
		segments.reserve(0x20);
	}

	explicit DetailedBlock(const Block &block) : DetailedBlock(block.blockID, block.data)
	{}

	DetailedBlock(const BlockID &blockID, const vector<Token> &tokens) : DetailedBlock(blockID)
	{
		segments.reserve(tokens.size());
		for (const auto &token : tokens)
			insertNewSegment(token);
	}

	static bool segmentsOverlap(const size_t &origin1, const size_t &end1, const size_t &origin2, const size_t &end2)
	{
		if (origin2 >= origin1 && origin2 < end1)
			return true;

		if (end2 > origin1 && end2 <= end1)
			return true;

		return (origin2 < origin1 && end2 > end1);
	}

	void insertNewSegment(const Token &token)
	{
		_insertNewSegment(segments, DetailedBlockMetadata(token, true));
	}

	void insertNewSegment(const DetailedBlockMetadata &metadataBlock)
	{
		_insertNewSegment(segments, metadataBlock);
	}

	void compactSegments()
	{
		if (segments.size() == 0)
			return;

		size_t length = segments.size() - 1, i = 0;
		while (i < length)
		{
			//The source of the data in those two chunks is contiguous and they have the same tag status
			if (segments[i].source.getAddress() + segments[i].length == segments[i + 1].source.getAddress()
				&& segments[i].tagged == segments[i + 1].tagged)
			{
				//We lengthen the first segment and delete the next
				segments[i].length += segments[i + 1].length;
				segments.erase(segments.begin() + i + 1);
				length -= 1;
			}
			else
				i += 1;
		}
	}

	bool fitSegmentInUntagged(const DetailedBlockMetadata &metadataBlock)
	{
		const size_t vectorSize = segments.size();
		sorted = false;

		//First, we try to fit the whole block
		for (size_t i = 0; i < vectorSize; ++i)
		{
			auto &segment = segments[i];
			if (!segment.tagged && segment.length >= metadataBlock.length)
			{
				if(segment.length != metadataBlock.length)
				{
					segment.destination += metadataBlock.length;
					segment.source += metadataBlock.length;
					segment.length -= metadataBlock.length;

					segments.insert(segments.begin() + i, metadataBlock);
				}
				else
				{
					segment.source = metadataBlock.source;
					segment.tagged = true;
				}

				return true;
			}
		}

		DetailedBlockMetadata localMeta = metadataBlock;

		//If we can't we split it in as many chunks as necessary (unoptimized)
		for (size_t i = 0; i < vectorSize; ++i)
		{
			auto &segment = segments[i];
			if (!segment.tagged)
			{
				//We overwrite fully the chunk
				if(segment.length <= localMeta.length)
				{
					const size_t segmentLength = segment.length;
					segment = localMeta;
					segment.length = segmentLength;

					localMeta.source += segmentLength;
					localMeta.destination += segmentLength;
					localMeta.length -= segmentLength;

					if(localMeta.length == 0)
						return true;
				}
				else
				{
					segment.destination += localMeta.length;
					segment.source += localMeta.length;
					segment.length -= localMeta.length;

					segments.insert(segments.begin() + i, localMeta);
					return true;
				}
			}
		}

		return false;
	}

	void trimUntagged(bool removeEverything = true)
	{
		for(size_t i = 0, length = segments.size(); i < length;)
		{
			if(!segments[i].tagged)
			{
				if(removeEverything)
				{
					segments.erase(segments.begin() + i);
					length -= 1;
				}
				else if(i + 1 < length && !segments[i + 1].tagged)
				{
					segments[i].length += segments[i + 1].length;
					segments.erase(segments.begin() + i + 1);
					length -= 1;
				}
				else
					i += 1;
			}
			else
			{
				i += 1;
			}
		}

		compactSegments();
	}

protected:

	void _insertNewSegment(vector<DetailedBlockMetadata> &segmentsVector, DetailedBlockMetadata metadataBlock)
	{
		size_t tokenAddress = metadataBlock.destination.getAddress();
		size_t vectorSize = segmentsVector.size();
		bool didWrite = false;
		
		if(vectorSize != 0 && (vectorSize & 0x1f) == 0)
			segmentsVector.reserve(vectorSize + 0x20);

		//If the array is sorted, we can start comparing when we start matching
		auto startOfSection = !sorted ? segmentsVector.begin() : lower_bound(segmentsVector.begin(), segmentsVector.end(), tokenAddress, [](const DetailedBlockMetadata & a, const size_t & b) { return a.destination.value + a.length < b; });

		//If iter = .end, then i == vectorSize and we skip the loop
		for (size_t i = static_cast<size_t>(startOfSection - segmentsVector.begin()); i < vectorSize; ++i)
		{
			DetailedBlockMetadata &meta = segmentsVector[i];

			//Identical match
			if (meta == metadataBlock)
			{
				//The tag isn't considered when comparing segments
				if (meta.tagged != metadataBlock.tagged)
					meta.tagged = metadataBlock.tagged;

				if(meta.willUntag != metadataBlock.willUntag)
					meta.willUntag = metadataBlock.willUntag;

				didWrite = true;
				break;
			}

			const size_t metaAddress = meta.destination.getAddress();
			const size_t endToken = tokenAddress + metadataBlock.length;
			const size_t endChunk = metaAddress + meta.length;

			if (segmentsOverlap(metaAddress, endChunk, tokenAddress, endToken))
			{
				DetailedBlockMetadata &metaBefore = meta;
				DetailedBlockMetadata metaAfter = meta;

				const bool tokenEndAfterChunk = endToken > endChunk;
				const bool tokenStartBeforeChunk = metaAddress >= tokenAddress;
				DetailedBlockMetadata newChunk = metadataBlock;

				//Shorten the unused metadata buffer before
				if(!tokenStartBeforeChunk)
				{
					metaBefore.length = tokenAddress - metaAddress;
				}

				//The new token is too large for the current segment, we use all the available space
				if(tokenEndAfterChunk)
				{
					//If the existing segment start before us, we compute the overlapping length.
					//If the new token start before the segment, we extend it backward. Imply the array is sorted
					const size_t overlappingSpace = tokenStartBeforeChunk ? endChunk - tokenAddress : (metaAfter.length - metaBefore.length);

					newChunk.length = overlappingSpace;
					metadataBlock.length -= overlappingSpace;
					metadataBlock.source += overlappingSpace;
					metadataBlock.destination += overlappingSpace;
					tokenAddress = metadataBlock.destination.value;
				}

				assert(newChunk.length <= BLOCK_SIZE);

				//Insert the new segment
				//	Can we recycle the previous segment?

				if (tokenStartBeforeChunk)
				{
					metaBefore = newChunk;
				}
				else if(newChunk.length > 0)
				{
					assert(metaBefore.length <= BLOCK_SIZE);
					segmentsVector.insert(segmentsVector.begin() + ++i, newChunk);
					vectorSize += 1;
				}

				//Is there still empty room after our token?
				if(!tokenEndAfterChunk)
				{
					const size_t spaceConsummed = metaAfter.length - (endChunk - endToken);

					metaAfter.length -= spaceConsummed;
					metaAfter.destination += spaceConsummed;
					metaAfter.source += spaceConsummed;

					assert(metaAfter.length <= BLOCK_SIZE);

					if(metaAfter.length > 0)
					{
						segmentsVector.insert(segmentsVector.begin() + ++i, metaAfter);
						vectorSize += 1;
					}
				}

				if(!tokenEndAfterChunk)
				{
					didWrite = true;
					break;
				}
			}

			//This token start after we end. No overlap in the future
			else if(sorted && endToken <= metaAddress)
			{
				break;
			}
		}

		if (!didWrite)
		{
			//We need to insert the metadataBlock
			assert(metadataBlock.length <= BLOCK_SIZE);

			//We fit at the back (hot path)
			if(segmentsVector.empty() || (segmentsVector.back().destination + segmentsVector.back().length) < metadataBlock.destination)
			{
				segmentsVector.emplace_back(metadataBlock);
			}
			else
			{
				//We need to find an insertion path. We already know there is no overlap
				for(auto iter = segmentsVector.cbegin(); iter != segmentsVector.cend(); ++iter)
				{
					if(iter->destination > metadataBlock.destination)
					{
						segmentsVector.insert(iter, metadataBlock);
						didWrite = true;
						break;
					}
				}

				if(!didWrite)
					segmentsVector.emplace_back(metadataBlock);
			}
		}
	}

	void splitAtIndex(vector<DetailedBlockMetadata> &segmentsVector, const Address & destinationToMatch, size_t length)
	{
		//We need to split the translation table so needToSkipCache can make a granular decision
		vector<DetailedBlockMetadata>::iterator section = lower_bound(segmentsVector.begin(), segmentsVector.end(), destinationToMatch, [](const DetailedBlockMetadata & a, const Address & b) { return (a.destination.value + a.length) <= b.value; });

		//Address is outside the translation table
		if(section == segmentsVector.end())
			return;

		if(!section->overlapWith(section->destination, section->length, destinationToMatch, 1))
			return;

		//The segment start before us
		if(section->destination.value != destinationToMatch.value)
		{
			const size_t delta = destinationToMatch.value - section->destination.value;

			//Preceding segment
			DetailedBlockMetadata newMeta = *section;
			newMeta.length = delta;

			section->destination += delta;
			section->source += delta;
			section->length -= delta;

			auto offset = section - segmentsVector.begin();

			segmentsVector.insert(section, newMeta);
			section = segmentsVector.begin() + offset + 1;
		}

		for(; length != 0 && section != segmentsVector.end(); ++section)
		{
			//The segment is already shorter
			if(section->length <= length)
			{
				length -= section->length;
			}
			//We're at the end, we need to split the segment
			else
			{
				DetailedBlockMetadata newMeta = *section;
				newMeta.length = length;

				section->destination += length;
				section->source += length;
				section->length -= length;
				length = 0;

				segmentsVector.insert(section, newMeta);
			}
		}
	}
};

struct CacheMemory : public DetailedBlock
{
	CacheMemory() : DetailedBlock(TMP_BUF) {}
	CacheMemory(const BlockID & block) : DetailedBlock(TMP_BUF)
	{
		segments.front().source = {block, 0};
	}
	explicit CacheMemory(const DetailedBlock & block) : DetailedBlock(block) {}

	void untagAll()
	{
		for(auto & segment : segments)
			segment.tagged = false;
	}

	void flush()
	{
		segments.clear();
		segments.emplace_back(DetailedBlockMetadata(TMP_BUF, BLOCK_SIZE));
	}

	bool isEmpty() const
	{
		for(const auto & segment : segments)
		{
			if(segment.tagged && segment.length > 0)
				return false;
		}

		return true;
	}

	int64_t availableRoom() const
	{
		int64_t room = BLOCK_SIZE;

		for(const auto & segment : segments)
		{
			if(segment.tagged && segment.length > 0)
				room -= segment.length;
		}

		return room;
	}

	vector<pair<size_t, bool>> segmentInCache(Address base, size_t length) const
	{
		vector<pair<size_t, bool>> output;
		size_t skipLength = 0;

		for(auto iter = segments.cbegin(); iter != segments.cend() && length != 0;)
		{
			if(iter->tagged && iter->overlapWith(base, length))
			{
				//The segment we're looking for start after the begining of the cache segment
				if(iter->source <= base)
				{
					const size_t shift = base.getAddress() - iter->source.getAddress();
					const size_t newSegmentLength = MIN(iter->length - shift, length);

					output.emplace_back(newSegmentLength, true);

					base += newSegmentLength;
					length -= newSegmentLength;

					iter = segments.cbegin();
					skipLength = 0;
				}
				else
				{
					skipLength = iter->source.getAddress() - base.getAddress();
					iter += 1;
				}
			}
			else
			{
				iter += 1;
			}

			//Does the segment start by a section not in the cache while later parts are?
			if(iter == segments.cend() && skipLength != 0)
			{
				output.emplace_back(skipLength, false);

				base += skipLength;
				length -= skipLength;
				iter = segments.cbegin();
				skipLength = 0;
			}
		}

		if(length)
			output.emplace_back(length, false);

		return output;
	}
};

//WARNING: We have NO protection against overlapping translations
//	Code have to be rock solid, which is a big problem (is it still true?)

/*
 * Virtual Memory construction:
 *
 * 	First, each provided block is added as sequential segments with the source and the destination identical
 * 	The resulting array is then sorted (by destination address) and copied in the translation table, that offer an indirection level
 * 	Then, each dependancy creates a new sub-segment at the destination address that tells where the data is currently located
 * 	The source of the data isn't affected by the insertion of a new sub-segment.
 *
 * 	/!\: Addresses in the cache have to be already translated
 */

struct VirtualMemory : public DetailedBlock
{
	//Source is the real address (where data come from)
	//Destination is the virtual address
	vector<DetailedBlockMetadata> translationTable;
	vector<DetailedBlockMetadata> translationsToPerform;
	unordered_set<BlockID> impactOfTranslationToPerform;

	bool hasCache;
	CacheMemory tmpLayout;

	bool hasCachedWrite;
	bool dumbCacheCommit;
	bool cachedWriteIgnoreLayout;
	size_t dumbCacheCommitLength;
	BlockID cachedWriteBlock;
	DetailedBlock cachedWriteRequest;

	VirtualMemory(const vector<Block> &blocks) : tmpLayout(), hasCache(false), hasCachedWrite(false), cachedWriteBlock(TMP_BUF), cachedWriteRequest()
	{
		size_t segmentLength = 0;
		translationTable.reserve(blocks.size());

		for (const Block &block : blocks)
		{
			translationTable.emplace_back(block.blockID);

			segmentLength += block.data.size();
			segments.reserve(segmentLength);

			for (const auto &token : block.data)
				insertNewSegment(token);
		}
	}

	VirtualMemory(const vector<Block> &blocks, const vector<size_t> &indexes) : tmpLayout(), hasCache(false), hasCachedWrite(false), cachedWriteBlock(TMP_BUF), cachedWriteRequest()
	{
		segments.reserve(indexes.size());

		for(const size_t & blockIndex : indexes)
			segments.emplace_back(blocks[blockIndex].blockID);

		sort(segments.begin(), segments.end());
		translationTable = segments;

		for(const size_t & blockIndex : indexes)
		{
			for (const auto &token : blocks[blockIndex].data)
				insertNewSegment(token);
		}
	}

	//We can't perform redirections one by one due to the risk of the cache origin becoming inconsistent
	//Basically, if we do that and overwrite the source of the data, writes would confuse the (obsolete) data in cache and the fresh data
	void staggeredRedirect(const Address &addressToRedirect, size_t length, const Address &newBaseAddress)
	{
		translationsToPerform.emplace_back(DetailedBlockMetadata(newBaseAddress, addressToRedirect, length, true));
		impactOfTranslationToPerform.insert(addressToRedirect.getBlock());
		splitAtIndex(translationTable, addressToRedirect, length);
	}

	void resizeTranslationTable(const size_t increment)
	{
		const size_t tableSize = translationTable.size();
		const size_t section = tableSize / 32;
		const size_t newSection = (tableSize + increment) / 32;

		if(newSection != section)
		{
			translationTable.reserve((newSection + 1) * 32);
		}
	}

	void performRedirect()
	{
		resizeTranslationTable(translationsToPerform.size());

		for(const auto & translation : translationsToPerform)
		{
			const BlockID & block = translation.destination.getBlock();

			//We ignore redirections for data outside the graph/network

			//Clearly outside
			if(segments.front().destination > block || segments.back().destination < block)
				continue;

			//Hole in the network?
			auto section = lower_bound(segments.begin(), segments.end(), block, [](const DetailedBlockMetadata & a, const BlockID & b) { return a.destination < b; });
			if(section != segments.end() && section->destination == block)
				_insertNewSegment(translationTable, translation);
		}

		translationsToPerform.clear();
		impactOfTranslationToPerform.clear();

		//FIXME: Should remove ASAP
		flushCache();
	}

	void redirect(const DetailedBlock & blockToRedirect, const BlockID & destinationBlock)
	{
		for (const auto &segment : blockToRedirect.segments)
			staggeredRedirect(segment.source, segment.length, Address(destinationBlock, segment.destination.getOffset()));
		performRedirect();
	}

	void didComplexLoadInCache(const CacheMemory &newTmpLayout)
	{
		hasCache = true;
		tmpLayout = newTmpLayout;
	}

	void flushCache()
	{
		hasCache = false;
		tmpLayout.flush();
	}

	void _translateSegment(Address from, size_t length, function<void(const Address&, const size_t, bool)>processing) const
	{
		bool foundTranslation = false;

#ifdef VERY_AGGRESSIVE_ASSERT
		bool finishedSection = false;
#endif

		//We detect the beginning of the potentially matching section
		auto iter = lower_bound(translationTable.begin(), translationTable.end(), from, [](const DetailedBlockMetadata & a, const Address from) { return a.destination.value < from.value; });
		while(iter != translationTable.begin() && (iter - 1)->fitWithinDestination(from))
			iter -= 1;

		//We iterate through it
		for(auto iterEnd = translationTable.end(); iter != iterEnd; ++iter)
		{
			if (iter->fitWithinDestination(from))
			{
#ifdef VERY_AGGRESSIVE_ASSERT
				assert(!finishedSection);
#endif
				foundTranslation = true;
				const size_t lengthLeft = iter->length - (from.getAddress() - iter->destination.getAddress());
				const size_t lengthToCopy = MIN(lengthLeft, length);
				const size_t shiftToReal = iter->source.getAddress() - iter->destination.getAddress();
				Address shiftedFrom(from + shiftToReal);

				const bool needIgnoreCache = needToSkipCache(shiftedFrom, from, lengthToCopy);

				processing(shiftedFrom, lengthToCopy, needIgnoreCache);

				if (lengthToCopy != length)
				{
					length -= lengthLeft;
					from += lengthLeft;
				}
				else
					break;
			}
			else
#ifdef VERY_AGGRESSIVE_ASSERT
				finishedSection = true;
#else
			break;
#endif
		}

		//External references may be outside the virtual memory
		if(!foundTranslation)
			processing(from, length, true);
	}

	void translateSegment(Address from, size_t length, vector<DetailedBlockMetadata> & output) const
	{
		output.reserve(4);
		_translateSegment(from, length, [&output](const Address& from, const size_t length, bool) {
			output.emplace_back(DetailedBlockMetadata(from, length));
		});
	}

	//The cache may be bypassed in some circumstances.
	//	A problem we face is when data A from page 0x1000 is loaded in the cache, then overwritten by B from page 0x2000.
	//		0x2000 is then erased and need A and B. If translations were performed after writes, B would read A, thinking 0x1000 cached it
	//	In order to fix it, translations are only performed after the cache is emptied
	//  Therefore, we know that cache are addressed pre-translation while any address pending translation need a direct read

	bool needToSkipCache(Address &read, Address &virtualRead, const size_t &length) const
	{
		//If the page isn't impacted in any way by translations, no need to iterate
		if(impactOfTranslationToPerform.find(read.getBlock()) == impactOfTranslationToPerform.end())
			return false;

		for(auto pendingTranslation = translationsToPerform.crbegin(), end = translationsToPerform.crend(); pendingTranslation != end; ++pendingTranslation)
		{
			//Can we find a translation redirecting our virtual address?
			if(pendingTranslation->overlapWith(virtualRead, length, pendingTranslation->destination, pendingTranslation->length))
			{
				//We make sure the segment fit entirely in this pending translation
				assert(pendingTranslation->fitWithin(pendingTranslation->destination, pendingTranslation->length, virtualRead));
				assert(pendingTranslation->fitWithin(pendingTranslation->destination, pendingTranslation->length, virtualRead + (length - 1)));
				read = pendingTranslation->source + (virtualRead.value - pendingTranslation->destination.value);
				return true;
			}
		}

		return false;
	}

	//WARNING: we assume that if we have to copy from the cache, the cache is caching a whole page (or at least everything up to a point)
	void _generateCopyWithTranslatedAddress(const Address &realFrom, const size_t &length, Address toward, bool ignoreCache, bool unTagCache, function<void(const Address &, const size_t, const Address &)> lambda)
	{
		//Could the data be in the cache?
		if (hasCache && !ignoreCache)
		{
			//We were up to no good and not only loaded the data in the cache but also messed with them
			//  Not only we need to lookup the cache layout but we need to support extra fragmentation within the cache
			//	The cache MUST contain the full content of a segment if the VM hasn't been updated
			//	Overwise, we assume the data left is still in place, in full ([cache | real | cache] isn't a supported layout)

			bool foundSomething = false;
			bool needAnotherRound;
			Address currentFrom = realFrom;
			size_t currentLength = length;

			do
			{
				needAnotherRound = false;

				vector<DetailedBlockMetadata> newTaggedSegments;

				for (auto &tmpSegment : tmpLayout.segments)
				{
					if(!tmpSegment.tagged)
						continue;

					//The source of the data match where the data at our offset is supposed to come from
					//FIXME: We're currently ignoring matches if the beginning isn't in it. This could be fixed by generating the COPY instruction for the beginning of the segment _BEFORE_ pulling more data from the cache (otherwise, when running the instructions, we could pad with 0 the spece we just overlooked (plus, CHAINED_COPY are more efficient than COPY's)
					if (DetailedBlockMetadata::fitWithin(tmpSegment.source, tmpSegment.length, currentFrom))
					{
						const size_t fromOffset = currentFrom.getAddress() - tmpSegment.source.getAddress();
						foundSomething = true;

						const size_t lengthLeft = tmpSegment.length - fromOffset;
						lambda(tmpSegment.destination + fromOffset, MIN(currentLength, lengthLeft), toward);

						if(unTagCache)
						{
							//We might have to fragment if the data we're getting isn't aligned on the cache buffer segments
							if(lengthLeft != tmpSegment.length || lengthLeft != currentLength)
							{
								DetailedBlockMetadata newMetadata(tmpSegment.source + fromOffset, tmpSegment.destination + fromOffset, currentLength, true);
								newMetadata.willUntag = true;
								newTaggedSegments.emplace_back(newMetadata);
							}
							else
								tmpSegment.willUntag = true;
						}

						if (lengthLeft < currentLength)
						{
							currentLength -= lengthLeft;
							currentFrom += lengthLeft;
							toward += lengthLeft;
							needAnotherRound = true;
						}
						else
						{
							currentLength = 0;
							break;
						}
					}
				}

				//Insert the untagged fragments if necessary
				for(const auto & fragment : newTaggedSegments)
					tmpLayout.insertNewSegment(fragment);

			} while (needAnotherRound);

			if (foundSomething)
			{
				if (currentLength != 0)
					lambda(currentFrom, currentLength, toward);

				return;
			}
		}

		lambda(realFrom, length, toward);
	}

	void applyCacheWillUntag()
	{
		for (auto &tmpSegment : tmpLayout.segments)
		{
			if(tmpSegment.tagged && tmpSegment.willUntag)
			{
				tmpSegment.tagged = false;
				tmpSegment.willUntag = false;
			}
		}
	}

	void generateCopyWithTranslatedAddress(const Address &realFrom, const size_t &length, Address toward, SchedulerData &commands, bool ignoreCache, bool unTagCache = false)
	{
		_generateCopyWithTranslatedAddress(realFrom, length, toward, ignoreCache, unTagCache, [&commands](const Address & from, const size_t length, const Address & toward)
		{
			commands.insertCommand({COPY, from, length, toward});
		});
	}

	struct LocalMetadata
	{
		Address virtualSource;
		Address source;
		Address destination;
		size_t length;

		LocalMetadata(const Address &_virtualSource, const Address &_source, const Address &_destination, const size_t & _length) : virtualSource(_virtualSource), source(_source), destination(_destination), length(_length) {}
	};

	void writeTaggedToBlock(const BlockID &destination, const DetailedBlock &metadata, SchedulerData &commands, bool ignoreLayout = false)
	{
		commands.newTransaction();
		size_t offset = 0;

		vector<LocalMetadata> canonicalCopy;
		canonicalCopy.reserve(metadata.segments.size());
		
		for(auto & segment : metadata.segments)
		{
			if(segment.tagged)
			{
				size_t sourceOffset = 0;
				//We want to perform a full translation (cache aware)
				_translateSegment(segment.source, segment.length, [&](const Address& from, const size_t length, bool ignoreCache) {
					_generateCopyWithTranslatedAddress(from, length, Address(destination, offset), ignoreCache, true, [&](const Address & from, const size_t length, const Address & toward) {
						canonicalCopy.emplace_back(LocalMetadata(segment.source + sourceOffset, from, toward, length));

						sourceOffset += length;
						offset += length;
					});
				});
			}
			else
				offset += segment.length;
		}

		applyCacheWillUntag();

#ifdef IGNORE_BLOCK_LAYOUT_UNLESS_FINAL
		//We can group writes by source (making fewer larger writes) if we don't care about the output layout
		if(ignoreLayout)
		{
			//Sort the copies by source with writes from the cache first (to leverage FLUSH_AND_PARTIAL_COMMIT)
			sort(canonicalCopy.begin(), canonicalCopy.end(), [](const LocalMetadata & a, const LocalMetadata & b) -> bool
			{
				if(a.source.getBlock() != b.source.getBlock())
				{
					if(a.source == TMP_BUF.getBlock())
						return true;

					else if(b.source == TMP_BUF.getBlock())
						return false;
				}

				return a.source < b.source;
			});

			Address toward(destination, 0);
			for(auto & copy : canonicalCopy)
			{
				staggeredRedirect(copy.virtualSource, copy.length, toward);
				copy.destination = toward;
				toward += copy.length;
			}
		}
		else
#else
		ignoreLayout = false;
#endif
		{
			//Update the virtual memory with the real segments to the real destination
			offset = 0;
			for(const auto & segment : metadata.segments)
			{
				if(segment.tagged)
				{
					if(segment.destination == destination)
						offset = segment.destination.getOffset();

					staggeredRedirect(segment.source, segment.length, Address(destination, offset));
					offset += segment.length;
				}
			}
		}

		for(const auto & copy : canonicalCopy)
			commands.insertCommand({COPY, copy.source, copy.length, copy.destination});

		commands.finishTransaction();
	}

	void cacheWrite(const BlockID block, const DetailedBlock & writes, bool ignoreLayout, SchedulerData &commands)
	{
		if(hasCachedWrite)
			commitCachedWrite(commands);

		cachedWriteIgnoreLayout = ignoreLayout;
		cachedWriteBlock = block;
		cachedWriteRequest = writes;
		hasCachedWrite = true;
		dumbCacheCommit = false;
#ifndef AVOID_UNECESSARY_REWRITE
		commitCachedWrite(commands);
#endif
	}

	void cacheBasicCacheWrite(const BlockID block, size_t length, SchedulerData & commands)
	{
		if(hasCachedWrite)
			commitCachedWrite(commands);

		hasCachedWrite = true;
		dumbCacheCommit = true;
		cachedWriteBlock = block;
		dumbCacheCommitLength = length;

#ifndef AVOID_UNECESSARY_REWRITE
		commitCachedWrite(commands);
#endif
	}

	void commitCachedWrite(SchedulerData &commands)
	{
		if(hasCachedWrite)
		{
			if(dumbCacheCommit)
			{
				commands.insertCommand({COPY, TMP_BUF, 0, dumbCacheCommitLength, cachedWriteBlock, 0});
				redirect(tmpLayout, cachedWriteBlock);
				tmpLayout.untagAll();
			}
			else
			{
				writeTaggedToBlock(cachedWriteBlock, cachedWriteRequest, commands, cachedWriteIgnoreLayout);
			}

			hasCachedWrite = false;

#ifdef VERY_AGGRESSIVE_ASSERT
			for(const auto & segment : tmpLayout.segments)
				assert(!segment.tagged);
#endif
			flushCache();
			performRedirect();
		}
	}

	void dropCachedWrite()
	{
		hasCachedWrite = false;
	}

	void iterateTranslatedSegments(Address from, size_t length, function<void(const Address&, const size_t, bool)> lambda) const
	{
		_translateSegment(from, length, lambda);
	}

	void loadTaggedToTMP(const DetailedBlock & dataToLoad, SchedulerData & commands, bool noTranslation = false);

private:
	void _loadTaggedToTMP(const DetailedBlock & dataToLoad, SchedulerData & commands, bool noTranslation);
};

#endif //HERMES_DETAILEDBLOCK_H
