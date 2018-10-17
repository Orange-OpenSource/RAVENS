/*-
 * Copyright 2003-2005 Colin Percival
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#if 0
__FBSDID("$FreextraBufferSD: src/usr.bin/bsdiff/bsdiff/bsdiff.c,v 1.1 2005/08/06 01:59:05 cperciva Exp $");
#endif

#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <err.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cassert>
#include <climits>

#define BSDIFF_PRIVATE

#include "../public_command.h"
#include "bsdiff.h"
#include <lzfx-4k/lzfx.h>
#include "../Encoding/encoder.h"
#include <layout.h>

#ifndef OFF_MAX
	#define OFF_MAX ~((off_t)1 << (sizeof(off_t) * 8 - 1))
#endif

using namespace std;

void bsdiff(const uint8_t * old, size_t oldSize, const uint8_t * newer, size_t newSize, vector<BSDiffPatch> & patch)
{
	auto * index = (off_t*) malloc((oldSize + 1) * sizeof(off_t));
	auto * value = (off_t*) malloc((oldSize + 1) * sizeof(off_t));

	if(index == nullptr || value == nullptr)
		err(1, "Malloc error");
	
	assert(oldSize <= OFF_MAX);

	qsufsort(index, value, old, (off_t) oldSize);

	free(value);

	size_t scan = 0, lastScan = 0;
	size_t matchPos = 0, matchLength = 0;
	size_t lastPos = 0, lastOffset = 0;

	while (scan < newSize)
	{
		size_t matchingBytes = 0;
		scan += matchLength;

		//Look for the longest matching pattern in old matching a slowly moving window in new
		for (size_t originalScanPos = scan; scan < newSize; scan++)
		{
			//Look for a matching byte sequence
			matchLength = search(index, old, oldSize, &newer[scan], newSize - scan, 0, oldSize, &matchPos);

			//Matching bytes from the beginning of the window (before shifting) of new, we will tolerate a couple of different bytes
			for (; originalScanPos < scan + matchLength; originalScanPos++)
			{
				if (originalScanPos + lastOffset < oldSize && old[originalScanPos + lastOffset] == newer[originalScanPos])
					matchingBytes++;
			}

			//If match is good enough
			if ((matchingBytes == matchLength && matchLength != 0) || matchLength > matchingBytes + 8)
				break;

			//Discard the original matching so we don't count it twice
			if (scan + lastOffset < oldSize && old[scan + lastOffset] == newer[scan])
				matchingBytes--;
		}

		//Is there a change or are we at the end of the new file (in which case we need to write the last data)
		if (matchingBytes != matchLength || scan == newSize)
		{
			size_t deltaLengthForward = 0;

			//Determine the length of the strike based on the ratio of identical bytes/bytes to patch
			// Start from where we left off since the last analysis
			for (size_t i = 0, strike = 0, strikeMax = 0; lastScan + i < scan && lastPos + i < oldSize;)
			{
				if (old[lastPos + i] == newer[lastScan + i])
					strike += 1;

				i += 1;

				//Magic ratio? 2 good bytes for one to patch
				if (strike * 2 - i > strikeMax * 2 - deltaLengthForward)
				{
					strikeMax = strike;
					deltaLengthForward = i;
				}
			}

			//Are we actually diffing and not just concatenating?
			size_t deltaLengthBackward = 0;
			if (scan < newSize)
			{
				//Read data backward from the match found by search()
				for (size_t i = 1, strike = 0, strikeMax = 0; lastScan + i <= scan && i <= matchPos; i++)
				{
					if (old[matchPos - i] == newer[scan - i])
						strike += 1;

					if (strike * 2 >= i && strike * 2 - i > strikeMax * 2 - deltaLengthBackward)
					{
						strikeMax = strike;
						deltaLengthBackward = i;
					}
				}
			}

			// Do those decent delta overlap?
			if (lastScan + deltaLengthForward > scan - deltaLengthBackward)
			{
				const off_t overlapWidth = (lastScan + deltaLengthForward) - (scan - deltaLengthBackward);
				const off_t baseOverlap = lastScan + deltaLengthForward - overlapWidth;
				off_t forwardStrikeExtension = 0;

				//Grow the forward delta since there was apparently enough to feed the backward delta (enough identical bytes to increase de ratio)
				//	This mean that data was deleted since old and we now need to find which has the most good bytes

				for (off_t i = 0, strike = 0, strikeMax = 0; i < overlapWidth; i++)
				{
					//More good bytes for starting at the beginning of the old buffer or the end?
					if (old[lastPos + deltaLengthForward - overlapWidth + i] == newer[baseOverlap + i])
						strike += 1;

					if (old[matchPos - deltaLengthBackward + i] == newer[baseOverlap + i])
						strike -= 1;

					//Let's enlarge slightly the forward delta
					if (strike > strikeMax)
					{
						strikeMax = strike;
						forwardStrikeExtension = i + 1;
					}
				}

				//deltaLengthForward recess before the overlap, then extend according to forwardStrikeExtension
				//deltaLengthBackward recess (forward) to leave the data for deltaLengthForward
				deltaLengthForward += forwardStrikeExtension - overlapWidth;
				deltaLengthBackward -= forwardStrikeExtension;
			}

			const size_t currentExtraBufferLength = (scan - deltaLengthBackward) - (lastScan + deltaLengthForward);

			uint8_t *deltaBuffer = nullptr, *extraBuffer = nullptr;

			//Delta computation
			if(deltaLengthForward)
			{
				deltaBuffer = (uint8_t *) malloc(deltaLengthForward);

				if (deltaBuffer == nullptr)
					errx(1, "Memory error allocatating delta buffer of size (%li)", deltaLengthForward);

				//Build the delta
				for (size_t i = 0; i < deltaLengthForward; i++)
					deltaBuffer[i] = newer[lastScan + i] - old[lastPos + i];
			}

			//Copy of the extra data
			if(currentExtraBufferLength)
			{
				extraBuffer = (uint8_t *) malloc(currentExtraBufferLength);
				if (deltaBuffer == nullptr)
					errx(1, "Memory error allocatating extra buffer of size (%li)", currentExtraBufferLength);

				memcpy(extraBuffer, &newer[lastScan + deltaLengthForward], currentExtraBufferLength);
			}

			if(deltaLengthForward || currentExtraBufferLength)
				patch.emplace_back(BSDiffPatch(lastPos, deltaLengthForward, deltaBuffer, currentExtraBufferLength, extraBuffer));

			lastScan = scan - deltaLengthBackward;
			lastPos = matchPos - deltaLengthBackward;
			lastOffset = matchPos - scan;
		}
	}

	// Free the memory we used
	free(index);
}

void bsdiff(const char * oldFile, const char * newFile, vector<BSDiffPatch> & patch)
{
	size_t oldSize;
	uint8_t *old = readFile(oldFile, &oldSize);

	size_t newSize;
	uint8_t *newer = readFile(newFile, &newSize);

	bsdiff(old, oldSize, newer, newSize, patch);

	free(old);
	free(newer);
}

void SchedulerPatch::compactBSDiff()
{
	if(bsdiff.size() < 2)
		return;
	
	std::vector<BSDiff> newBSDiff;
	newBSDiff.reserve(bsdiff.size());
	newBSDiff.emplace_back(bsdiff.front());

	for(auto bsdiffIter = bsdiff.cbegin() + 1; bsdiffIter != bsdiff.cend(); ++bsdiffIter)
	{
		//We can extend the BSDiff
		if(newBSDiff.back().extra.length == 0)
		{
			newBSDiff.back().extra = bsdiffIter->extra;

			const size_t nextDeltaLength = bsdiffIter->delta.length;
			auto & deltaSegment = newBSDiff.back().delta;

			deltaSegment.data = (uint8_t* ) realloc(deltaSegment.data, deltaSegment.length + nextDeltaLength);
			memcpy(&deltaSegment.data[deltaSegment.length], bsdiffIter->delta.data, nextDeltaLength);
			free(bsdiffIter->delta.data);
			deltaSegment.length += nextDeltaLength;
		}
		else if(bsdiffIter->delta.length == 0)
		{
			const size_t nextExtraLength = bsdiffIter->extra.length;
			auto & extraSegment = newBSDiff.back().extra;

			extraSegment.data = (uint8_t* ) realloc(extraSegment.data, extraSegment.length + nextExtraLength);
			memcpy(&extraSegment.data[extraSegment.length], bsdiffIter->extra.data, nextExtraLength);
			free(bsdiffIter->extra.data);
			extraSegment.length += nextExtraLength;
		}
		else
		{
			newBSDiff.emplace_back(*bsdiffIter);
		}
	}
	
	bsdiff = newBSDiff;
}

bool writeBSDiff(const SchedulerPatch & patch, void * output)
{
	size_t length;
	uint8_t * encodedCommands = nullptr;
	Encoder encoder;
	encoder.encode(patch.commands, encodedCommands, length);

	if(encodedCommands == nullptr)
		return false;

	if(fwrite(encodedCommands, length, 1, (FILE *) output) != 1)
	{
		free(encodedCommands);
		return false;
	}
	free(encodedCommands);

	//Write the magic value
	const uint32_t bsdiffMagicValue = BSDIFF_MAGIC;
	if(fwrite(&bsdiffMagicValue, 1, sizeof(uint32_t), (FILE*) output) != sizeof(uint32_t))
		return false;

	//Write the offset
	if(fwrite(&patch.startAddress, 1, sizeof(uint32_t), (FILE*) output) != sizeof(uint32_t))
		return false;

	size_t fullUncompressedLength = 2 * sizeof(uint16_t);	//Number of BSDiff segments and number of validation ranges

	for(const auto & command : patch.bsdiff)
	{
		assert(command.delta.length > 0 && command.delta.length < UINT32_MAX);
		assert(command.extra.length < UINT32_MAX);

		fullUncompressedLength += 2 * sizeof(uint32_t) + command.delta.length + command.extra.length;
	}

	//Add the space necessary for validation
	fullUncompressedLength += patch.newRanges.size() * sizeof(UpdateFinalHash);

	auto * uncompressedBuffer = (uint8_t*) malloc(fullUncompressedLength);
	if(uncompressedBuffer == nullptr)
		return false;

	assert(patch.bsdiff.size() < UINT32_MAX);

	//Copy the number of segments
	*((uint32_t*) uncompressedBuffer) = static_cast<uint32_t>(patch.bsdiff.size());

	size_t index = sizeof(uint32_t);
	for(const auto & command : patch.bsdiff)
	{
		offtout(static_cast<uint32_t>(command.delta.length), &uncompressedBuffer[index]);
		index += sizeof(uint32_t);

		memcpy(&uncompressedBuffer[index], command.delta.data, command.delta.length);
		index += command.delta.length;

		offtout(static_cast<uint32_t>(command.extra.length), &uncompressedBuffer[index]);
		index += sizeof(uint32_t);

		memcpy(&uncompressedBuffer[index], command.extra.data, command.extra.length);
		index += command.extra.length;
	}

	assert(patch.newRanges.size() < UINT16_MAX);
	*(uint16_t *) &uncompressedBuffer[index] = static_cast<uint16_t>(patch.newRanges.size());
	index += sizeof(uint16_t);

	//Copy the ranges the bootloader need to verify
	for(const auto & range : patch.newRanges)
	{
		UpdateFinalHash verif{};

		verif.start = range.start;
		verif.length = range.length;

		if(hydro_hex2bin(verif.hash, sizeof(verif.hash), range.expectedHash.c_str(), range.expectedHash.size(), nullptr, nullptr) == sizeof(verif.hash))
		{
			memcpy(&uncompressedBuffer[index], &verif, sizeof(verif));
			index += sizeof(verif);
		}
	}

	size_t compressedLength = fullUncompressedLength + 200;
	auto * compressedBuffer = (uint8_t*) malloc(compressedLength);
	if(compressedBuffer == nullptr)
	{
		free(uncompressedBuffer);
		return false;
	}

	int retValue = lzfx_compress(uncompressedBuffer, index - 1, compressedBuffer, &compressedLength);

	free(uncompressedBuffer);

	if(retValue != 0)
	{
		free(compressedBuffer);
		return false;
	}

	if(fwrite(compressedBuffer, compressedLength, 1, (FILE*) output) != 1)
	{
		free(compressedBuffer);
		return false;
	}

	free(compressedBuffer);
	return true;
}
