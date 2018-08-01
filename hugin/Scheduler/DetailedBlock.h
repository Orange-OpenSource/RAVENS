//
// Created by Emile-Hugo Spir on 3/15/18.
//

#ifndef HERMES_DETAILEDBLOCK_H
#define HERMES_DETAILEDBLOCK_H

//Source is where the data we need currently is
//Destination is where the data belong

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

	void insertNewSegment(const Token &token, bool skipResize = false)
	{
		_insertNewSegment(segments, DetailedBlockMetadata(token, true), skipResize);
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

	void trimUntagged()
	{
		for(size_t i = 0, length = segments.size(); i < length;)
		{
			if(!segments[i].tagged)
			{
				if(i + 1 < length && !segments[i + 1].tagged)
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

		for(auto & segment : segments)
		{
			if(!segment.tagged)
				segment.source = segment.destination;
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

protected:

	void _insertNewSegment(vector<DetailedBlockMetadata> &segmentsVector, DetailedBlockMetadata metadataBlock, bool skipResize = false)
	{
		size_t tokenAddress = metadataBlock.destination.getAddress();
		size_t vectorSize = segmentsVector.size();
		bool didWrite = false;

		if(!skipResize)
		{
			if(vectorSize != 0 && (vectorSize & 0x1f) == 0)
				segmentsVector.reserve(vectorSize + 0x20);
		}

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
				//If we're sorted, we can skip the full iteration
				if(!sorted || vectorSize < 100)
				{
					for(auto iter = segmentsVector.cbegin(); iter != segmentsVector.cend(); ++iter)
					{
						if(iter->destination > metadataBlock.destination)
						{
							segmentsVector.insert(iter, metadataBlock);
							didWrite = true;
							break;
						}
					}
				}
				else
				{
					auto iter = lower_bound(segmentsVector.begin(), segmentsVector.end(), metadataBlock.destination, [](const DetailedBlockMetadata & a, const Address & b)
					{
						return a.destination <= b;
					});

					if(iter != segmentsVector.end())
					{
						segmentsVector.insert(iter, metadataBlock);
						didWrite = true;
					}
				}

				if(!didWrite)
					segmentsVector.emplace_back(metadataBlock);
			}
		}
	}
};

#endif //HERMES_DETAILEDBLOCK_H
