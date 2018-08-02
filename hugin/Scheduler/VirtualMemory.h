//
// Created by Emile-Hugo Spir on 8/1/18.
//

#ifndef HERMES_VIRTUALMEMORY_H
#define HERMES_VIRTUALMEMORY_H

#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <functional>

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

struct TranslationTable
{
	unordered_map<BlockID, DetailedBlock> translationData;
	vector<DetailedBlockMetadata> translationsToPerform;
	unordered_set<BlockID> impactOfTranslationToPerform;

	TranslationTable(const vector<Block> &blocks)
	{
		translationData.reserve(blocks.size());

		//Initialize a simple page for each BlockID
		for (const auto &block : blocks)
		{
			auto & array =  translationData[block.blockID].segments;

			array.emplace_back(DetailedBlockMetadata(block.blockID));
			array.reserve(0x100);

		}
	}

	TranslationTable(const vector<Block> &blocks, const vector<size_t> &indexes)
	{
		translationData.reserve(blocks.size());

		//Initialize a simple page for each BlockID
		for (const auto index : indexes)
		{
			const auto &blockID = blocks[index].blockID;
			auto & array =  translationData[blockID].segments;

			array.emplace_back(DetailedBlockMetadata(blockID));
			array.reserve(0x100);
		}
	}

	void splitAtIndex(const Address & destinationToMatch, size_t length)
	{
		auto translationArray = translationData.find(destinationToMatch.getBlock());
		if(translationArray != translationData.end())
		{
			translationArray->second.splitAtIndex(translationArray->second.segments, destinationToMatch, length);
		}
	}

	//We can't perform redirections one by one due to the risk of the cache origin becoming inconsistent
	//Basically, if we do that and overwrite the source of the data, writes would confuse the (obsolete) data in cache and the fresh data
	void staggeredRedirect(const Address &addressToRedirect, size_t length, const Address &newBaseAddress)
	{
		translationsToPerform.emplace_back(DetailedBlockMetadata(newBaseAddress, addressToRedirect, length, true));
		impactOfTranslationToPerform.insert(addressToRedirect.getBlock());
		splitAtIndex(addressToRedirect, length);
	}

	void performRedirect()
	{
		for(const auto & translation : translationsToPerform)
		{
			const BlockID & block = translation.destination.getBlock();

			//We ignore redirections for data outside the graph/network

			auto translationArray = translationData.find(block);
			if(translationArray != translationData.end())
			{
				//Translations must fit in a single page
				assert((translation.destination.value & BLOCK_OFFSET_MASK) + translation.length <= BLOCK_SIZE);
				translationArray->second.insertNewSegment(translation);
			}
		}

		translationsToPerform.clear();
		impactOfTranslationToPerform.clear();
	}

	void translateSegment(Address from, size_t length, function<void(const Address&, const size_t, bool)>processing) const
	{
#ifdef VERY_AGGRESSIVE_ASSERT
		bool finishedSection = false;
#endif

		assert((from.value & BLOCK_OFFSET_MASK) + length <= BLOCK_SIZE);

		//We detect the beginning of the potentially matching section
		const auto currentTranslation = translationData.find(from.getBlock());

		//The address is outside the virtual memory
		if(currentTranslation == translationData.end())
		{
			return processing(from, length, true);
		}

		const auto & currentTranslationData = currentTranslation->second.segments;

		auto iter = lower_bound(currentTranslationData.begin(), currentTranslationData.end(), from, [](const DetailedBlockMetadata & a, const Address from) { return a.destination.value < from.value; });
		while(iter != currentTranslationData.begin() && (iter - 1)->fitWithinDestination(from))
			iter -= 1;

		//We iterate through it
		for(auto iterEnd = currentTranslationData.end(); iter != iterEnd && length != 0; ++iter)
		{
			if (iter->fitWithinDestination(from))
			{
#ifdef VERY_AGGRESSIVE_ASSERT
				assert(!finishedSection);
#endif
				const size_t lengthLeft = iter->length - (from.getAddress() - iter->destination.getAddress());
				const size_t lengthToCopy = MIN(lengthLeft, length);
				const size_t shiftToReal = iter->source.getAddress() - iter->destination.getAddress();
				Address shiftedFrom(from + shiftToReal);

				const bool needIgnoreCache = needToSkipCache(shiftedFrom, from, lengthToCopy);

				processing(shiftedFrom, lengthToCopy, needIgnoreCache);

				length -= lengthToCopy;
				from += lengthToCopy;
			}
			else
#ifdef VERY_AGGRESSIVE_ASSERT
				finishedSection = true;
#else
			break;
#endif
		}

		//Have we performed the translation in full
		assert(length == 0);
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
	TranslationTable translationTable;

	bool hasCache;
	CacheMemory tmpLayout;

	bool hasCachedWrite;
	bool dumbCacheCommit;
	bool cachedWriteIgnoreLayout;
	size_t dumbCacheCommitLength;
	BlockID cachedWriteBlock;
	DetailedBlock cachedWriteRequest;

	VirtualMemory(const vector<Block> &blocks) : tmpLayout(), translationTable(blocks), hasCache(false), hasCachedWrite(false), cachedWriteBlock(TMP_BUF), cachedWriteRequest()
	{
		size_t segmentLength = 0;
		for (const Block &block : blocks)
			segmentLength += block.data.size();

		segments.reserve(segmentLength);

		for (const Block &block : blocks)
		{
			segments.emplace_back(block.blockID);
			for (const auto &token : block.data)
				insertNewSegment(token);
		}
	}

	VirtualMemory(const vector<Block> &blocks, const vector<size_t> &indexes) : tmpLayout(), translationTable(blocks, indexes), hasCache(false), hasCachedWrite(false), cachedWriteBlock(TMP_BUF), cachedWriteRequest()
	{
		size_t segmentLength = 0;
		for(const size_t & blockIndex : indexes)
			segmentLength += blocks[blockIndex].data.size();

		segments.reserve(segmentLength);
		for(const size_t & blockIndex : indexes)
		{
			segments.emplace_back(blocks[blockIndex].blockID);
			for (const auto &token : blocks[blockIndex].data)
				insertNewSegment(token, true);
		}
	}

	void performRedirect()
	{
		translationTable.performRedirect();

		//FIXME: Should remove ASAP
		flushCache();
	}

	void redirect(const DetailedBlock & blockToRedirect, const BlockID & destinationBlock)
	{
		for (const auto &segment : blockToRedirect.segments)
			translationTable.staggeredRedirect(segment.source, segment.length, Address(destinationBlock, segment.destination.getOffset()));
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

	void translateSegment(Address from, size_t length, vector<DetailedBlockMetadata> & output) const
	{
		output.reserve(4);
		translationTable.translateSegment(from, length, [&output](const Address& from, const size_t length, bool) {
			output.emplace_back(DetailedBlockMetadata(from, length));
		});
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
				translationTable.translateSegment(segment.source, segment.length, [&](const Address& from, const size_t length, bool ignoreCache) {
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
				translationTable.staggeredRedirect(copy.virtualSource, copy.length, toward);
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

					translationTable.staggeredRedirect(segment.source, segment.length, Address(destination, offset));
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

			performRedirect();
#ifdef VERY_AGGRESSIVE_ASSERT
			for(const auto & segment : tmpLayout.segments)
				assert(!segment.tagged);
#endif
		}
	}

	void dropCachedWrite()
	{
		hasCachedWrite = false;
	}

	void iterateTranslatedSegments(Address from, size_t length, function<void(const Address&, const size_t, bool)> lambda) const
	{
		translationTable.translateSegment(from, length, lambda);
	}

	void loadTaggedToTMP(const DetailedBlock & dataToLoad, SchedulerData & commands, bool noTranslation = false);

private:
	void _loadTaggedToTMP(const DetailedBlock & dataToLoad, SchedulerData & commands, bool noTranslation);
};


#endif //HERMES_VIRTUALMEMORY_H
