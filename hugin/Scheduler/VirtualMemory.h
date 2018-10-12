/*
 * Copyright (C) 2018 Orange
 *
 * This software is distributed under the terms and conditions of the 'BSD-3-Clause-Clear'
 * license which can be found in the file 'LICENSE.txt' in this package distribution
 * or at 'https://spdx.org/licenses/BSD-3-Clause-Clear.html'.
 */

/**
 * @author Emile-Hugo Spir
 */

#ifndef HERMES_VIRTUALMEMORY_H
#define HERMES_VIRTUALMEMORY_H

#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <functional>

struct CacheMemory : public DetailedBlock
{
	CacheMemory() : DetailedBlock(CACHE_BUF) {}
	CacheMemory(const BlockID & block) : DetailedBlock(CACHE_BUF)
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
		segments.emplace_back(DetailedBlockMetadata(CACHE_BUF, BLOCK_SIZE));
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

	vector<DetailedBlockMetadata> segmentInCache(Address base, size_t length) const
	{
		vector<DetailedBlockMetadata> output;
		size_t skipLength = 0;
		
#ifdef VERY_AGGRESSIVE_ASSERT
		const size_t realLength = length;
#endif
		//This algorithm iterate the cache, and look for segments containing its head (base).
		//	If it can't find its head, it's looking for the shortest `skipLength`, which will be used to skip ahead and mark the ignored area as missing from the cache
		//This isn't a straightforward implementation because fragments of the segment we're looking for can be in any order

		for(auto iter = segments.cbegin(); iter != segments.cend() && length != 0;)
		{
			if(iter->tagged && iter->overlapWith(base, length))
			{
				//The segment we're looking for start after the begining of the cache segment
				if(iter->source <= base)
				{
					const size_t shift = base.getAddress() - iter->source.getAddress();
					const size_t newSegmentLength = MIN(iter->length - shift, length);

					assert(newSegmentLength != 0);
					output.emplace_back(iter->destination + shift, base, newSegmentLength, true);

					base += newSegmentLength;
					length -= newSegmentLength;

					iter = segments.cbegin();
					skipLength = 0;
				}
				else
				{
					//We look for the shortest distance we would need to jump forward by to find a new token
					const size_t skipToToken = iter->source.getAddress() - base.getAddress();
					if(skipLength == 0 || skipToToken < skipLength)
						skipLength = skipToToken;
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
				output.emplace_back(base, skipLength, false);

				base += skipLength;
				length -= skipLength;
				iter = segments.cbegin();
				skipLength = 0;
			}
		}

		if(length)
			output.emplace_back(base, length, false);
		
#ifdef VERY_AGGRESSIVE_ASSERT
		size_t returnedLength = 0;
		for(const auto & segment : output)
			returnedLength += segment.length;

		assert(returnedLength == realLength);
#endif

		return output;
	}
};

struct TranslationTable
{
	unordered_map<BlockID, DetailedBlock> translationData;
	vector<DetailedBlockMetadata> translationsToPerform;

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

	//FIXME: Get rid of staggered redirects as they don't really serve any purpose nowadays
	void staggeredRedirect(const Address &addressToRedirect, size_t length, const Address &newBaseAddress)
	{
		assert(length > 0);
		translationsToPerform.emplace_back(DetailedBlockMetadata(newBaseAddress, addressToRedirect, length, true));
	}

	void performRedirect()
	{
		for(const auto & translation : translationsToPerform)
		{
			const BlockID & block = translation.destination.getBlock();

			//We ignore redirections for data outside the network

			auto translationArray = translationData.find(block);
			if(translationArray != translationData.end())
			{
				//Translations must fit in a single page
				assert((translation.destination.value & BLOCK_OFFSET_MASK) + translation.length <= BLOCK_SIZE);
				translationArray->second.insertNewSegment(translation);
			}
		}

		translationsToPerform.clear();
	}

	void translateSegment(Address from, size_t length, function<void(const Address&, const size_t)>processing) const
	{
#ifdef VERY_AGGRESSIVE_ASSERT
		bool finishedSection = false;
#endif

		while((from.value & BLOCK_OFFSET_MASK) + length > BLOCK_SIZE)
		{
			const size_t spaceLeft = BLOCK_SIZE - (from.value & BLOCK_OFFSET_MASK);
			const size_t writeToPerform = MIN(spaceLeft, length);
			translateSegment(from, writeToPerform, processing);
			
			from += writeToPerform;
			length -= writeToPerform;
		}

		//We detect the beginning of the potentially matching section
		const auto currentTranslation = translationData.find(from.getBlock());

		//The address is outside the virtual memory
		if(currentTranslation == translationData.end())
		{
			return processing(from, length);
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
				const int64_t shiftToReal = iter->source.getAddress() - iter->destination.getAddress();
				Address shiftedFrom(from + shiftToReal);

				processing(shiftedFrom, lengthToCopy);

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

struct VirtualMemory
{
	//Source is the real address (where data come from)
	//Destination is the virtual address
	TranslationTable translationTable;

	CacheMemory cacheLayout;

	bool hasCachedWrite;
	bool cachedWriteIgnoreLayout;
	BlockID cachedWriteBlock;
	DetailedBlock cachedWriteRequest;

	VirtualMemory(const vector<Block> &blocks) : cacheLayout(), translationTable(blocks), hasCachedWrite(false), cachedWriteBlock(CACHE_BUF), cachedWriteRequest() {}

	VirtualMemory(const vector<Block> &blocks, const vector<size_t> &indexes) : cacheLayout(), translationTable(blocks, indexes), hasCachedWrite(false), cachedWriteBlock(CACHE_BUF), cachedWriteRequest() {}

	void performRedirect()
	{
		translationTable.performRedirect();
	}

	void flushCache()
	{
		cacheLayout.flush();
	}

	void translateSegment(Address from, size_t length, vector<DetailedBlockMetadata> & output) const
	{
		output.reserve(4);
		translationTable.translateSegment(from, length, [&output](const Address& from, const size_t length) {
			output.emplace_back(DetailedBlockMetadata(from, length));
		});
	}

	void generateCopyWithTranslatedAddress(const Address &realFrom, size_t length, Address toward, bool unTagCache, function<void(const Address &, const size_t, const Address &)> lambda)
	{
		//Is the data in the cache?
		if (realFrom.getBlock() == CACHE_BUF)
		{
#ifdef VERY_AGGRESSIVE_ASSERT
			{
				bool hasCache = false;
				for(const auto & cacheToken : cacheLayout.segments)
				{
					if(cacheToken.tagged)
					{
						hasCache = true;
						break;
					}
				}
				assert(hasCache);
			}
#endif
			
			//The logic is fairly straightforward. We copy the data from the cache, then untag it if necessary
			lambda(realFrom, length, toward);
			
			if(unTagCache)
			{
				auto search = lower_bound(cacheLayout.segments.begin(), cacheLayout.segments.end(), realFrom, [](const DetailedBlockMetadata & meta, const Address & from) { return meta.destination <= from; });
				
				assert(search != cacheLayout.segments.begin());
				search -= 1;
				assert(search->destination <= realFrom);
				
				Address fromCopy = realFrom;
				vector<DetailedBlockMetadata> untagSegments;
				while (fromCopy >= search->destination && length != 0)
				{
					const size_t shift = fromCopy.value - search->destination.value;
					const size_t curLength = MIN(search->length - shift, length);
					
					untagSegments.emplace_back(search->source + shift, fromCopy, curLength, true);
					untagSegments.back().willUntag = true;
					
					fromCopy += curLength;
					length -= curLength;
					search += 1;
				}

				assert(length == 0);
				for(const auto & untag : untagSegments)
					cacheLayout.insertNewSegment(untag);
			}
		}
		else
			lambda(realFrom, length, toward);
	}

	void applyCacheWillUntag()
	{
		for (auto &tmpSegment : cacheLayout.segments)
		{
			if(tmpSegment.tagged && tmpSegment.willUntag)
			{
				tmpSegment.tagged = false;
				tmpSegment.willUntag = false;
			}
		}
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
				//We detect whether part of the segment are in the cache. If so, no need to translate
				size_t sourceOffset = 0;
				const auto arePartsInCache = cacheLayout.segmentInCache(segment.source, segment.length);
				for(const auto & subSection : arePartsInCache)
				{
					//Is in cache?
					if(subSection.tagged)
					{
						generateCopyWithTranslatedAddress(subSection.source, subSection.length, Address(destination, offset), true, [&](const Address & from, const size_t length, const Address & toward) {
							assert(length > 0 && sourceOffset + length <= segment.length);

							canonicalCopy.emplace_back(LocalMetadata(segment.source + sourceOffset, from, toward, length));

							sourceOffset += length;
							offset += length;
						});
					}
					else
					{
						translationTable.translateSegment(subSection.source, subSection.length, [&](const Address& translatedFrom, const size_t translatedLength) {
							generateCopyWithTranslatedAddress(translatedFrom, translatedLength, Address(destination, offset), true, [&](const Address & from, const size_t length, const Address & toward) {
								canonicalCopy.emplace_back(LocalMetadata(segment.source + sourceOffset, from, toward, length));
								
								sourceOffset += length;
								offset += length;
							});
						});
					}
				}
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
					if(a.source == CACHE_BUF.getBlock())
						return true;

					else if(b.source == CACHE_BUF.getBlock())
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
		performRedirect();

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
#ifndef AVOID_UNECESSARY_REWRITE
		commitCachedWrite(commands);
#endif
	}

	void commitCachedWrite(SchedulerData &commands)
	{
		if(hasCachedWrite)
		{
			writeTaggedToBlock(cachedWriteBlock, cachedWriteRequest, commands, cachedWriteIgnoreLayout);
			hasCachedWrite = false;

#ifdef VERY_AGGRESSIVE_ASSERT
			for(const auto & segment : cacheLayout.segments)
				assert(!segment.tagged);
#endif
		}
	}

	void dropCachedWrite()
	{
		hasCachedWrite = false;
	}

	void iterateTranslatedSegments(Address from, size_t length, function<void(const Address&, const size_t)> lambda) const
	{
		translationTable.translateSegment(from, length, lambda);
	}

	void loadTaggedToTMP(const DetailedBlock & dataToLoad, SchedulerData & commands);

private:
	void _sortBySortedAddress(const DetailedBlock & dataToLoad, vector<DetailedBlockMetadata> & output);
	void _retagReusedToken(vector<DetailedBlockMetadata> & sortedTokenWithTranslation);
	void _loadTaggedToTMP(const DetailedBlock & dataToLoad, SchedulerData & commands);
};


#endif //HERMES_VIRTUALMEMORY_H
