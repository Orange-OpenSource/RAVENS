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

#include "scheduler.h"

void extractSubSectionToLoad(const DetailedBlockMetadata & toExtract, const CacheMemory & cache, vector<DetailedBlockMetadata> & output)
{
	output.emplace_back(toExtract);

	for(auto & curTmp : cache.segments)
	{
		if(!curTmp.tagged)
			continue;

		for(size_t index = 0, outputLength = output.size(); index < outputLength; )
		{
			auto & originalSegment = output[index];

			// We're not tagging segments that are now in use. Might be a problem but would generate a ton of noise for _loadTaggedToTMP
			if(curTmp.overlapWith(originalSegment.source, originalSegment.length))
			{
				//Partial overlap?
				if(curTmp.source > originalSegment.source || curTmp.source + curTmp.length < originalSegment.source + originalSegment.length)
				{
					//We're starting before, so we add back the segment before the match
					if(originalSegment.source < curTmp.source)
					{
						output.emplace_back(DetailedBlockMetadata(output[index].source, curTmp.source.getAddress() - originalSegment.source.getAddress()));
						outputLength += 1;
					}
					
					//We're finishing after, so we add the segment after the match
					if(originalSegment.source + originalSegment.length > curTmp.source + curTmp.length)
					{
						const size_t offsetCacheEndToTranslation = (curTmp.source.value + curTmp.length) - originalSegment.source.value;
						output.emplace_back(DetailedBlockMetadata(output[index].source + offsetCacheEndToTranslation,
																  originalSegment.source.value + originalSegment.length - (curTmp.source.value + curTmp.length)));
						outputLength += 1;
					}
				}

				//If we removed sections of the segment, we must remove the initial segment from output
				output.erase(output.begin() + index);
				outputLength -= 1;

				if(outputLength == 0)
					return;
			}
			else
				index += 1;
		}
	}
}

void VirtualMemory::_sortBySortedAddress(const DetailedBlock & dataToLoad, vector<DetailedBlockMetadata> & output)
{
	if(dataToLoad.segments.size() == 1)
	{
		output = dataToLoad.segments;
		return;
	}
	
	output.clear();
	output.reserve(dataToLoad.segments.size());
	
	for(auto & segment : dataToLoad.segments)
	{
		if(!segment.tagged)
			continue;
		
		size_t offset = 0;
		iterateTranslatedSegments(segment.source, segment.length, [&](const Address & translation, const size_t &length)
		{
			output.emplace_back(segment.source + offset, translation, length, true);
			offset += length;
		});
	}
	
	sort(output.begin(), output.end(), [](const DetailedBlockMetadata & a, const DetailedBlockMetadata & b) { return a.destination < b.destination; });
}

void VirtualMemory::_retagReusedToken(vector<DetailedBlockMetadata> & sortedTokenWithTranslation)
{
	//It may be interesting to retag token already in the cache so that we don't generate unecessary COPYs
#warning "To Implement"
}

void VirtualMemory::_loadTaggedToTMP(const DetailedBlock & dataToLoad, SchedulerData & commands)
{
	CacheMemory tmpLayoutCopy = cacheLayout;
	commands.newTransaction();
	
	vector<DetailedBlockMetadata> sortedChunks;
	_sortBySortedAddress(dataToLoad, sortedChunks);

	for (const auto &realSegment : sortedChunks)
	{
		if (!realSegment.tagged)
			continue;

		//We determine precisely what we need to load
		vector<DetailedBlockMetadata> subSegments;
		extractSubSectionToLoad(realSegment, tmpLayoutCopy, subSegments);

		for(const auto & segment : subSegments)
		{
			//We try to find room somewhere in the cache
			bool foundGoodMatch = false;
			auto tmpSegment = tmpLayoutCopy.segments.begin();
			while(tmpSegment != tmpLayoutCopy.segments.end())
			{
				if(!tmpSegment->tagged && tmpSegment->length >= segment.length)
				{
					foundGoodMatch = true;
					break;
				}
				else
					tmpSegment += 1;
			}

			//If couldn't find anything, we defragment
			if(!foundGoodMatch)
			{
				//Restart the iterator and make room
				tmpSegment = tmpLayoutCopy.segments.begin();

				//Make the necessary room in CACHE_BUF
				//If data in use or if we're done with the current segment
				while((tmpSegment->tagged || tmpSegment->length == 0) && tmpSegment != tmpLayoutCopy.segments.end())
					tmpSegment += 1;

				assert(tmpSegment != tmpLayoutCopy.segments.end());

				//We need to make room
				while(tmpSegment->length < segment.length)
				{
					//This usually mean we don't have enough room in the cache, which is not supposed to be possible as our caller check for that. This assert is _big_ trouble, and probably means extractSubSectionToLoad is failing
					assert((tmpSegment + 1) != tmpLayoutCopy.segments.end());

					auto currentSegment = *tmpSegment, nextSegment = *(tmpSegment + 1);

					//We move the content of the next segment to our current index and get some extra space thanks to that
					commands.insertCommand({COPY, nextSegment.destination, nextSegment.length, currentSegment.destination});

					//Update the cache context
					tmpSegment->source = nextSegment.source;
					tmpSegment->length = nextSegment.length;
					tmpSegment->tagged = true;
					tmpSegment += 1;
					tmpSegment->source = currentSegment.source + nextSegment.length;
					tmpSegment->destination = currentSegment.destination + nextSegment.length;
					tmpSegment->length = currentSegment.length;
					tmpSegment->tagged = false;

					if(tmpSegment != tmpLayoutCopy.segments.end())
					{
						while((tmpSegment + 1) != tmpLayoutCopy.segments.end() && !(tmpSegment + 1)->tagged)
						{
							tmpSegment->length += (tmpSegment + 1)->length;
							tmpLayoutCopy.segments.erase(tmpSegment + 1);
						}

						if((tmpSegment + 1) == tmpLayoutCopy.segments.end())
							tmpSegment->source = tmpSegment->destination;
					}
				}
			}

			Address tmpBuffer = tmpSegment->destination;
			size_t segmentShift = 0;
			iterateTranslatedSegments(segment.source, segment.length, [&](const Address & translation, const size_t &length)
			{
				generateCopyWithTranslatedAddress(translation, length, tmpBuffer + segmentShift, false, [&commands](const Address & from, const size_t translatedLength, const Address & toward)
				{
					commands.insertCommand({COPY, from, translatedLength, toward});
				});

				
				//We mark from where the data come from
				tmpLayoutCopy.insertNewSegment({segment.source + segmentShift, tmpBuffer + segmentShift, length, true});
				segmentShift += length;
			});

			//We must NEVER excess the capacity of CACHE_BUF)
			assert(tmpLayoutCopy.availableRoom() >= 0);
		}
	}

	commands.finishTransaction();
	cacheLayout = tmpLayoutCopy;
}

void VirtualMemory::loadTaggedToTMP(const DetailedBlock & dataToLoad, SchedulerData & commands)
{
	cacheLayout.trimUntagged();

#ifdef VERY_AGGRESSIVE_ASSERT
	//We check if we have enough room in the cache to load our data
	size_t lengthAlreadyInCache = 0, lengthToFitInCache = 0;

	//Room already in use
	for(const auto & segment : cacheLayout.segments)
	{
		if(segment.tagged)
			lengthAlreadyInCache += segment.length;
	}

	//Data to load
	for(const auto & segment : dataToLoad.segments)
	{
		if(segment.tagged)
		{
			//We might have some duplicate
			vector<DetailedBlockMetadata> subSegments;
			extractSubSectionToLoad(segment, cacheLayout, subSegments);

			for(const auto & subSegment : subSegments)
				lengthToFitInCache += subSegment.length;

		}
	}

	//If this assert fail, we're trying to load too much data in the cache
	assert(lengthAlreadyInCache + lengthToFitInCache <= BLOCK_SIZE);
#endif

	_loadTaggedToTMP(dataToLoad, commands);
}
