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

#include <cstring>
#include <chrono>
#include "scheduler.h"

size_t _realBlockSizeBit = BLOCK_SIZE_BIT_DEFAULT;
size_t _realFullAddressSpace = FLASH_SIZE_BIT_DEFAULT;

void schedule(const vector<BSDiffMoves> & input, vector<PublicCommand> & output, bool printStats)
{
	vector<Block> blockStructure;

	if(!buildBlockVector(input, blockStructure))
		return;

	SchedulerData scheduler;

	scheduler.wantLog = printStats;

	//This pass is redundant with removeUnidirectionnalReferences but is a bit faster as less complicated
	Scheduler::removeSelfReferencesOnly(blockStructure, scheduler);

	Scheduler::removeUnidirectionnalReferences(blockStructure, scheduler);

	Scheduler::removeNetworks(blockStructure, scheduler);

	scheduler.generateInstructions(output);

	if(printStats)
		scheduler.printStats(output);
}

#include "bsdiff/bsdiff.h"
#include "validation.h"

size_t trimBSDiff(vector<BSDiffPatch> &patch)
{
	size_t lengthTrimmed = 0;
	BSDiffPatch & lastPatch = patch.back();
	if(lastPatch.lengthExtra == 0)
	{
		size_t trim = lastPatch.lengthDelta;
		while(trim != 0 && lastPatch.deltaData[--trim] == 0);

		//Extra padding present, we can trim it!
		if(trim != lastPatch.lengthDelta - 1)
		{
			lengthTrimmed = lastPatch.lengthDelta - trim - 1;
			
			//We may trim, but some data are still left
			if(trim != 0 || lastPatch.deltaData[0] != 0)
				lastPatch.lengthDelta = trim + 1;

				//No data left, the last patch is pointless
			else
				patch.pop_back();
		}
	}
	return lengthTrimmed;
}

bool stripDeltaBelowThreshold(vector<BSDiffPatch> &patch, size_t & earlySkip, const uint32_t threshold)
{
	BlockID currentPage(earlySkip);
	Address baseAddressIter(earlySkip);
	
#ifdef PRINT_BSDIFF_SECTIONS_STATUS
	cout << "[DEBUG] BSDiff status: Starting address: 0x" << hex << baseAddressIter.value << dec << endl;
#endif
	
	size_t amountOfDeltaInPage = 0;
	bool deletedSomething = false;
	
	//We look for how much delta there is in any page
	for(auto iter = patch.begin(); iter != patch.end(); )
	{
		assert(baseAddressIter == currentPage);
		
		if(baseAddressIter + iter->lengthDelta + iter->lengthExtra == currentPage)
		{
			amountOfDeltaInPage += iter->lengthDelta;
			baseAddressIter += iter->lengthDelta + iter->lengthExtra;

			if(iter->lengthExtra == 0)
				iter->extraPos = baseAddressIter.value;
			else
				assert(baseAddressIter.value == iter->extraPos + iter->lengthExtra);

			iter += 1;
			continue;
		}
		//Okay, we're leaving the page
		const size_t lengthLeftInPage = BLOCK_SIZE - baseAddressIter.getOffset();
		const size_t sectionOfDeltaFallingInPrevPage = MIN(iter->lengthDelta, lengthLeftInPage);
		const size_t sectionOfDeltaFallingInNextPage = (iter->lengthDelta - sectionOfDeltaFallingInPrevPage) % BLOCK_SIZE;
		const size_t originalTokenSize = iter->lengthDelta + iter->lengthExtra;
		const size_t numberOfPagesToSkip = (originalTokenSize - lengthLeftInPage) / BLOCK_SIZE;
		
		amountOfDeltaInPage += sectionOfDeltaFallingInPrevPage;
		
		assert(amountOfDeltaInPage <= BLOCK_SIZE);
#ifdef PRINT_BSDIFF_SECTIONS_STATUS
		cout << "[DEBUG] BSDiff status: Page 0x" << hex << currentPage.value << " contains a total of 0x" << amountOfDeltaInPage << " bytes of delta (threshold is 0x" << threshold << ")." << dec << endl;
#endif
		
		//Do we have enough delta? If not, we need to delete the delta from the last page
		if(amountOfDeltaInPage < threshold)
		{
			//Soooo... We will need to convert any delta from the page to extra :|
			deletedSomething = true;
			
			//We trim the last page
			if(sectionOfDeltaFallingInPrevPage)
			{
				//Can we remove the full delta section?
				if(sectionOfDeltaFallingInPrevPage == iter->lengthDelta)
				{
					free(iter->deltaData);
					iter->deltaData = nullptr;
					iter->lengthDelta = 0;
					iter->lengthExtra += sectionOfDeltaFallingInPrevPage;
					iter->extraPos -= sectionOfDeltaFallingInPrevPage;
				}
				else
				{
					//Are we at the beginning?
					if(iter == patch.begin())
					{
						assert(sectionOfDeltaFallingInPrevPage == BLOCK_SIZE);
						earlySkip += BLOCK_SIZE;
					}
					else
					{
						(iter - 1)->lengthExtra += sectionOfDeltaFallingInPrevPage;
					}

					//We need to move the delta data to a smaller buffer
					iter->oldDataAddress += sectionOfDeltaFallingInPrevPage;
					iter->lengthDelta -= sectionOfDeltaFallingInPrevPage;
					memmove(iter->deltaData, &iter->deltaData[sectionOfDeltaFallingInPrevPage], iter->lengthDelta);
					
					uint8_t * buffer = (uint8_t *) realloc(iter->deltaData, iter->lengthDelta);
					assert(buffer);
					iter->deltaData = buffer;
				}
			}
			
			//Go back in the array and shrink delta while we're still in the same page
			size_t currentPosInBuffer = baseAddressIter.getOffset();
			auto iterCopy = iter;
			while(currentPosInBuffer)
			{
				assert(iterCopy != patch.begin());
				iterCopy -= 1;

				//Our starting point is in the extra section
				if(currentPosInBuffer <= iterCopy->lengthExtra)
					break;

				currentPosInBuffer -= iterCopy->lengthExtra;

				//Do we need to fully wipe the current delta segment?
				if(currentPosInBuffer >= iterCopy->lengthDelta)
				{
					currentPosInBuffer -= iterCopy->lengthDelta;
					iterCopy->extraPos -= iterCopy->lengthDelta;
					iterCopy->lengthExtra += iterCopy->lengthDelta;
					
					free(iterCopy->deltaData);
					iterCopy->deltaData = nullptr;
					iterCopy->lengthDelta = 0;

				}
				//Nah, we're almost good
				else
				{
					iterCopy->lengthDelta -= currentPosInBuffer;
					iterCopy->extraPos -= currentPosInBuffer;
					iterCopy->lengthExtra += currentPosInBuffer;

					uint8_t * buffer = (uint8_t *) realloc(iterCopy->deltaData, iterCopy->lengthDelta);
					assert(buffer);
					iterCopy->deltaData = buffer;
					break;
				}
			}
		}
		
		//Okay, does the next page fully fit within this token, halfway between the delta and extra section?
		if(sectionOfDeltaFallingInNextPage != 0 && sectionOfDeltaFallingInNextPage + iter->lengthExtra >= BLOCK_SIZE)
		{
			// :(
			if(sectionOfDeltaFallingInNextPage < threshold)
			{
				iter->lengthDelta -= sectionOfDeltaFallingInNextPage;
				iter->extraPos -= sectionOfDeltaFallingInNextPage;
				iter->lengthExtra += sectionOfDeltaFallingInNextPage;
				
				uint8_t * buffer = (uint8_t *) realloc(iter->deltaData, iter->lengthDelta);
				assert(buffer);
				iter->deltaData = buffer;
			}

			amountOfDeltaInPage = 0;
		}
		else
		{
			amountOfDeltaInPage = sectionOfDeltaFallingInNextPage;
		}
		
		//We update the context for the next page
		currentPage += numberOfPagesToSkip + 1;
		baseAddressIter += originalTokenSize;
		
		assert(amountOfDeltaInPage <= baseAddressIter.value);
		
		iter += 1;
	}
	
#ifdef VERY_AGGRESSIVE_ASSERT
	{
		Address testAddress(earlySkip);
		for(const auto & iter : patch)
		{
			if(iter.lengthExtra != 0)
				assert(testAddress.value + iter.lengthDelta == iter.extraPos);
			testAddress += iter.lengthDelta + iter.lengthExtra;
		}
	}
#endif
	
	return deletedSomething;
}

bool generatePatch(const uint8_t *original, size_t originalLength, const uint8_t *newer, size_t newLength, SchedulerPatch &outputPatch, bool printStats)
{
	outputPatch.clear(false);

	//We look for an identical prefix
	size_t earlySkip = 0;
	for(size_t smallest = MIN(originalLength, newLength);
				earlySkip < smallest && original[earlySkip] == newer[earlySkip];
				++earlySkip);

	//We won't have to diff this part
	earlySkip &= BLOCK_MASK;
	outputPatch.startAddress = earlySkip >> BLOCK_SIZE_BIT;

	vector<BSDiffPatch> patch;

	//Generate the diff
	//TODO: Introduce a skip field, to go over vast untouched area faster
	{
#ifdef PRINT_SPEED
		auto beginBSDiff = chrono::high_resolution_clock::now();
#endif
		bsdiff(original + earlySkip, originalLength - earlySkip, newer + earlySkip, newLength - earlySkip, patch);
#ifdef PRINT_SPEED
		auto endBSDiff = chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endBSDiff - beginBSDiff).count();
		cout << "Performing BSDiff in " << duration << " ms." << endl;
#endif
	}

	if(earlySkip)
	{
		for(auto & diff : patch)
			diff.oldDataAddress += earlySkip;
	}

	//Before processing the diff, we check it actually works
	if(!validateBSDiff(original, originalLength, newer, newLength, patch, earlySkip))
		return false;

	//We apply the threshold
	if(stripDeltaBelowThreshold(patch, earlySkip, BSDIFF_DELTA_REMOVAL_THRESHOLD))
	{
#ifdef VERY_AGGRESSIVE_ASSERT
		assert(validateBSDiff(original, originalLength, newer, newLength, patch, earlySkip));
#endif
	}

	//If we don't have extra at the end, we may be able to trim the delta.
	size_t lengthTrimmed = trimBSDiff(patch);

	if(printStats)
	{
		size_t newData = 0;

		for(auto diff : patch)
			newData += diff.lengthExtra;

		cout << "Valid BSDiff with " << newData << " bytes of new data" << endl;
	}

	if(patch.empty())
	{
		cout << "Files are identical. If not the case, please open a bug report." << endl;
		return true;
	}

	//Craft the BSDiffMoves (the moves to perform)
	vector<BSDiffMoves> moves;
	moves.reserve(patch.size());

	size_t currentAddress = earlySkip;
	for(const auto & cur : patch)
	{
		if(cur.lengthDelta != 0)
		{
			assert(cur.oldDataAddress + cur.lengthDelta <= originalLength);
			moves.emplace_back(BSDiffMoves(cur.oldDataAddress, cur.lengthDelta, currentAddress));
		}

		currentAddress += cur.lengthDelta + cur.lengthExtra;
		
		uint8_t * extraData = nullptr;
		if(cur.lengthExtra)
		{
			extraData = (uint8_t *) malloc(cur.lengthExtra);
			assert(extraData != nullptr);
			assert(cur.extraPos + cur.lengthExtra <= newLength);
			memcpy(extraData, &newer[cur.extraPos], cur.lengthExtra);
		}

		outputPatch.bsdiff.push_back(BSDiff {
				.delta = {
						.data = cur.deltaData,
						.length = cur.lengthDelta
				},

				.extra = {
						.data = extraData,
						.length = cur.lengthExtra
				}
		});
	}

	assert(newLength - currentAddress == lengthTrimmed);

	//Extend the last copy if we had to trim it so that the end of the last block is copied
	if(lengthTrimmed)
	{
		assert(!moves.empty());
		assert(moves.back().start + moves.back().length + lengthTrimmed <= originalLength);
		moves.back().length += lengthTrimmed;
	}

	if(outputPatch.bsdiff.empty())
		return false;

	outputPatch.compactBSDiff();

	//Generate the commands to run
	{
#ifdef PRINT_SPEED
		auto begin = chrono::high_resolution_clock::now();
#endif
		schedule(moves, outputPatch.commands, printStats);
#ifdef PRINT_SPEED
		auto end = chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
		cout << "Performing conflict resolution in " << duration << " ms." << endl;
#endif
	}

	{
#ifdef PRINT_SPEED
		auto begin = chrono::high_resolution_clock::now();
#endif
		generateVerificationRangesPrePatch(outputPatch, earlySkip);
		generateVerificationRangesPostPatch(outputPatch, earlySkip, newLength);
#ifdef PRINT_SPEED
		auto end = chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
		cout << "Generating conflict ranges in " << duration << " ms." << endl;
#endif
	}

	computeExpectedHashForRanges(outputPatch.oldRanges, original, originalLength);
	computeExpectedHashForRanges(outputPatch.newRanges, newer, newLength);

	return true;
}
