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

void extractSubSectionToLoad(const DetailedBlockMetadata & toExtract, const CacheMemory & cache, const VirtualMemory & virtualMemory, bool noTranslation, vector<DetailedBlockMetadata> & output)
{
	output.emplace_back(toExtract);

	for(auto & curTmp : cache.segments)
	{
		if(!curTmp.tagged)
			continue;

		for(size_t index = 0, outputLength = output.size(); index < outputLength; )
		{
			auto & originalSegment = output[index];

			//Translate the segment
			vector<DetailedBlockMetadata> translatedSegment;
			if(noTranslation)
				translatedSegment.emplace_back(originalSegment);
			else
				virtualMemory.translateSegment(originalSegment.source, originalSegment.length, translatedSegment);

			bool didUntag = false;
			size_t translationLength = 0;
			for(auto & segment : translatedSegment)
			{
				// We're not tagging segments that are now in use. Might be a problem but would generate a ton of noise for _loadTaggedToTMP
				if(curTmp.overlapWith(segment.source, segment.length))
				{
					//Partial overlap?
					if(curTmp.source > segment.source || curTmp.source + curTmp.length < segment.source + segment.length)
					{
						//We're starting before, so we add back the segment before the match
						if(segment.source < curTmp.source)
						{
							output.emplace_back(DetailedBlockMetadata(output[index].source + translationLength, curTmp.source.getAddress() - segment.source.getAddress()));
							outputLength += 1;
						}

						//We're finishing after, so we add the segment after the match
						if(segment.source + segment.length > curTmp.source + curTmp.length)
						{
							const size_t offsetCacheEndToTranslation = (curTmp.source.value + curTmp.length) - segment.source.value;
							output.emplace_back(DetailedBlockMetadata(output[index].source + translationLength + offsetCacheEndToTranslation,
																	  segment.source.value + segment.length - (curTmp.source.value + curTmp.length)));
							outputLength += 1;
						}
					}

					segment.tagged = false;
					didUntag = true;
				}
				else
					segment.tagged = true;

				translationLength += segment.length;
			}

			//If we removed sections of the segment, we must remove/fragment the output
			if(didUntag)
			{
				auto original = output[index];
				size_t length = 0;

				output.erase(output.begin() + index);
				outputLength -= 1;

				for(const auto & translation : translatedSegment)
				{
					if(translation.tagged)
					{
						output.insert(output.begin() + index++, DetailedBlockMetadata(original.source + length, original.destination + length, translation.length, true));
						outputLength += 1;
					}

					length += translation.length;
				}

				if(outputLength == 0)
					return;
			}
			else
				index += 1;
		}
	}
}

void buildWriteCommandToFlushCacheFromNodes(const vector<NetworkNode> & nodes, const VirtualMemory & virtualMemory, const BlockID & destination, DetailedBlock & commands)
{
	for(const auto & node : nodes)
	{
		for(const auto & token : node.tokens)
		{
			if(token.cleared)
				continue;

			for(const auto & segment : token.sourceToken)
			{
				//Get the section NOT in the cache
				vector<DetailedBlockMetadata> sectionNotPresent;
				extractSubSectionToLoad(segment, virtualMemory.tmpLayout, virtualMemory, false, sectionNotPresent);

				//If everything is, that's simpler
				if(sectionNotPresent.empty())
				{
					commands.insertNewSegment(segment);
					continue;
				}

				//If nothing is in the cache, we can skip
				if(sectionNotPresent.size() == 1 && sectionNotPresent.front().length == segment.length)
				{
					continue;
				}

				//We have fragments not in the cache, that sucks. First, let's sort the pieces recovered by source
				sort(sectionNotPresent.begin(), sectionNotPresent.end(), [](const DetailedBlockMetadata & a, const DetailedBlockMetadata & b)
				{
					return a.source.value < b.source.value;
				});

				//We then get a "head" starting at the beginning of the segment
				Address baseSegment = segment.origin;
				size_t length = segment.length;

				for(const auto & section : sectionNotPresent)
				{
					//Do we have a section in the cache before this segment?
					if(baseSegment.value < section.source.value)
					{
						const size_t deltaLength = section.source.value - baseSegment.value;
						commands.insertNewSegment(DetailedBlockMetadata(baseSegment, deltaLength, true));
					}

					const size_t jumpForward = section.source.value + section.length;
					const size_t deltaJump = jumpForward - baseSegment.value;
					assert(deltaJump <= length);

					baseSegment.value = jumpForward;
					length -= deltaJump;
				}

				if(length != 0)
					commands.insertNewSegment(DetailedBlockMetadata(baseSegment, length, true));
			}
		}
	}

	//Replace the destination by a sequence of addresses
	Address outputAddress(destination, 0);
	for(auto segment : commands.segments)
	{
		segment.destination = outputAddress;
		outputAddress += segment.length;
	}
}

#ifdef TMP_STRATEGY_PROGRESSIVE
void VirtualMemory::_loadTaggedToTMP(const DetailedBlock & dataToLoad, SchedulerData & commands, bool noTranslation)
{
	CacheMemory tmpLayoutCopy = tmpLayout;
	commands.newTransaction();

	for (const auto &realSegment : dataToLoad.segments)
	{
		if (!realSegment.tagged)
			continue;

		//We determine precisely what we need to load
		vector<DetailedBlockMetadata> subSegments;
		extractSubSectionToLoad(realSegment, tmpLayoutCopy, *this, noTranslation, subSegments);

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

				//Make the necessary room in TMP_BUF
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

			if(noTranslation)
			{
				if (segment.source.getOffset() == 0 && segment.length == BLOCK_SIZE && tmpLayoutCopy.isEmpty())
					commands.insertCommand({COPY, segment.source, segment.length, TMP_BUF});
				else
					commands.insertCommand({COPY, segment.source, segment.length, tmpSegment->destination});

				//We mark from where the data come from
				tmpLayoutCopy.insertNewSegment({segment.source, tmpSegment->destination, segment.length, true});
			}
			else
			{
				Address tmpBuffer = tmpSegment->destination;
				iterateTranslatedSegments(segment.source, segment.length, [&](const Address & address, const size_t length, bool ignoreCache)
				{
					if (address.getOffset() == 0 && length == BLOCK_SIZE && tmpLayoutCopy.isEmpty())
						commands.insertCommand({COPY, segment.source, segment.length, TMP_BUF});
					else
						generateCopyWithTranslatedAddress(address, length, tmpBuffer, commands, ignoreCache, false);

					//We mark from where the data come from
					tmpLayoutCopy.insertNewSegment({address, tmpBuffer, length, true});
					tmpBuffer += length;
				});
			}

			//We must NEVER excess the capacity of TMP_BUF)
			assert(tmpLayoutCopy.availableRoom() >= 0);
		}
	}

	commands.finishTransaction();
	tmpLayout = tmpLayoutCopy;
}

#else

void trimUntaggedSpaceInTmp(CacheMemory & tmpLayout, SchedulerData & commands)
{
	commands.newTransaction();

	Address readHead = TMP_BUF, writeHead = TMP_BUF;

	for(auto & tmpSegment : tmpLayout.segments)
	{
		if(tmpSegment.tagged)
		{
			if(readHead.getOffset() != writeHead.getOffset())
			{
				commands.insertCommand({COPY, readHead, tmpSegment.length, writeHead});
				tmpSegment.destination = writeHead;
			}

			writeHead += tmpSegment.length;
		}

		readHead += tmpSegment.length;
	}

	commands.finishTransaction();

	//Now, some book keeping in tmpLayout

	//Remove all untagged blocks
	tmpLayout.segments.erase(remove_if(tmpLayout.segments.begin(), tmpLayout.segments.end(),
									   [](const DetailedBlockMetadata & block)
									   { return !block.tagged; }), tmpLayout.segments.end());

	//Add a final block using all the available space at the end
	const size_t finalOffset = tmpLayout.segments.empty() ? 0 : tmpLayout.segments.back().getFinalDestOffset();

	if(finalOffset != BLOCK_SIZE)
		tmpLayout.insertNewSegment(DetailedBlockMetadata(TMP_BUF + finalOffset, BLOCK_SIZE - finalOffset, false));

	tmpLayout.compactSegments();
}

void VirtualMemory::_loadTaggedToTMP(const DetailedBlock & dataToLoad, SchedulerData & commands, bool noTranslation)
{
	CacheMemory tmpLayoutCopy = tmpLayout;
	trimUntaggedSpaceInTmp(tmpLayoutCopy, commands);

	Address tmpBuffer = tmpLayoutCopy.segments.back().destination;

	commands.newTransaction();
	for (const auto &segment : dataToLoad.segments)
	{
		if (segment.tagged)
		{
			if(noTranslation)
			{
				if (segment.source.getOffset() == 0 && segment.length == BLOCK_SIZE && tmpBuffer.getOffset() == 0)
					commands.insertCommand({COPY, segment.source, segment.length, TMP_BUF});
				else
					commands.insertCommand({COPY, segment.source, segment.length, tmpBuffer});

				tmpLayoutCopy.insertNewSegment({segment.source, tmpBuffer, segment.length, true});
				tmpBuffer += segment.length;
			}
			else
			{
				iterateTranslatedSegments(segment.source, segment.length, [&, segment](const Address & address, const size_t length, bool ignoreCache)
				{
					if (address.getOffset() == 0 && length == BLOCK_SIZE && tmpBuffer.getOffset() == 0)
						commands.insertCommand({COPY, segment.source, segment.length, TMP_BUF});
					else
						generateCopyWithTranslatedAddress(address, length, tmpBuffer, commands, ignoreCache, false);

					tmpLayoutCopy.insertNewSegment({address, tmpBuffer, length, true});
					tmpBuffer += length;
				});
			}

			//We must NEVER excess the capacity of TMP_BUF)
			assert(tmpLayoutCopy.availableRoom() >= 0);
		}
	}

	commands.finishTransaction();
	tmpLayout = tmpLayoutCopy;
}
#endif

void VirtualMemory::loadTaggedToTMP(const DetailedBlock & dataToLoad, SchedulerData & commands, bool noTranslation)
{
	if(!hasCache)
		didComplexLoadInCache(CacheMemory());
	else
		tmpLayout.trimUntagged();

#ifdef VERY_AGGRESSIVE_ASSERT
	//We check if we have enough room in the cache to load our data
	size_t lengthToFitInCache = 0;

	//Room already in use
	for(const auto & segment : tmpLayout.segments)
	{
		if(segment.tagged)
			lengthToFitInCache += segment.length;
	}

	//Data to load
	for(const auto & segment : dataToLoad.segments)
	{
		if(segment.tagged)
		{
			//We might have some duplicate
			vector<DetailedBlockMetadata> subSegments;
			extractSubSectionToLoad(segment, tmpLayout, *this, noTranslation, subSegments);

			for(const auto & subSegment : subSegments)
				lengthToFitInCache += subSegment.length;

		}
	}

	//If this assert fail, we're trying to load too much data in the cache
	assert(lengthToFitInCache <= BLOCK_SIZE);
#endif

	_loadTaggedToTMP(dataToLoad, commands, noTranslation);
}
