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

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include <memory.h>
#include <sys/param.h>
#include "../Bytecode/execution.h"
#include "../core.h"
#include "../../common/layout.h"
#include "lzfx_light.h"
#include "bsdiff.h"

extern const UpdateMetadata updateMetadataMain;
extern const UpdateMetadata updateMetadataSec;

extern const uint8_t backupCache1[BLOCK_SIZE];
extern const uint8_t backupCache2[BLOCK_SIZE];

HERMES_CRITICAL const uint8_t * getBuffer(const size_t traceCounter)
{
	return traceCounter & 2u ? backupCache2 : backupCache1;
}

HERMES_CRITICAL void savePageToBuffer(const size_t blockAddress, const size_t traceCounter)
{
	const uint8_t * cache = getBuffer(traceCounter);

	erasePage((size_t) cache);
	writeToNAND((size_t) cache, sizeof(backupCache1), (const uint8_t *) (blockAddress & ~BLOCK_OFFSET_MASK));
}

HERMES_CRITICAL uint8_t consumeByte(BSDiffContext * context)
{
	if(context->currentCacheOffset >= context->lengthLeft)
	{
		context->lengthLeft = sizeof(cacheRAM);
		lzfx_decompress(&context->lzfx, &context->lengthLeft);
		context->currentCacheOffset = 0;

		if(context->lengthLeft < 1)
		{
			context->isOutOfData = true;
			return 0;
		}
	}

	uint8_t output = cacheRAM[context->currentCacheOffset];

	context->currentCacheOffset += 1;

	return output;
}

HERMES_CRITICAL uint16_t consumeWord(BSDiffContext * context)
{
	if(context->currentCacheOffset + 2 > context->lengthLeft)
	{
		union {
			uint16_t word;
			uint8_t byte[2];
		} word;

		word.byte[0] = consumeByte(context);
		word.byte[1] = consumeByte(context);

		return word.word;
	}

	uint16_t output = *(uint16_t*) (cacheRAM + context->currentCacheOffset);

	context->currentCacheOffset += 2;

	return output;
}

HERMES_CRITICAL uint32_t consumeDWord(BSDiffContext * context)
{
	//Not enough room to get the full DWord in one go in the available buffer
	if(context->currentCacheOffset + 4 > context->lengthLeft)
	{
		union {
			uint32_t dword;
			uint16_t word[2];
		} dword;

		dword.word[0] = consumeWord(context);
		dword.word[1] = consumeWord(context);

		return dword.dword;
	}

	uint32_t output = *(uint32_t*) (cacheRAM + context->currentCacheOffset);

	context->currentCacheOffset += 4;

	return output;
}

HERMES_CRITICAL uint64_t consumeQWord(BSDiffContext * context)
{
	union {
		uint64_t qword;
		uint32_t dword[2];
	} qword;

	qword.dword[0] = consumeDWord(context);
	qword.dword[1] = consumeDWord(context);

	return qword.qword;
}

HERMES_CRITICAL bool performValidation(BSDiffContext * context, bool dryRun)
{
	//We at least need a word. This means we ran out of data before, which is bad
	if(context->isOutOfData)
		return false;

	uint16_t numberValidation = consumeWord(context);

	while(numberValidation--)
	{
		const uint32_t startOfHash = consumeDWord(context);
		const uint32_t length = consumeWord(context) + 1;
		uint8_t refHash[HASH_LENGTH];

		for(uint8_t i = 0; i < HASH_LENGTH; ++i)
		{
			if(context->isOutOfData)
				return false;

			refHash[i] = consumeByte(context);
		}

		if(!dryRun)
		{
			uint8_t computed[HASH_LENGTH];
			hashMemory((const void *) (uintptr_t) startOfHash, length, computed);

			if(memcmp(computed, refHash, sizeof(refHash)) != 0)
				return false;
		}
	}

	return true;
}

HERMES_CRITICAL void addByteToOutputBuffer(uint8_t byte, size_t virtualAddress, uint8_t * currentCounter)
{
	static uint8_t writeBuffer[WRITE_GRANULARITY];

	if(*currentCounter >= sizeof(writeBuffer))
		return;

	writeBuffer[*currentCounter] = byte;

	//Is the buffer full
	if(*currentCounter == sizeof(writeBuffer) - 1)
	{
		//We shift the address to align with the size of the buffer
		writeToNAND(virtualAddress & ~(sizeof(writeBuffer) - 1), sizeof(writeBuffer), writeBuffer);
		*currentCounter = 0;
	}
	else
		*currentCounter += 1;
}

/*
 * The BSDiff data structure is the following:
 *
 * typedef struct
 *	{
 *		uint32_t flag = BSDIFF_MAGIC;
 *		uint16_t numberSegments;
 *
 *		struct
 *		{
 *			uint16_t lengthDelta;
 *			char delta[lengthDelta];
 *
 *			uint16_t lengthInsert;
 *			char extra[lengthInsert];
 *
 *		} bsdiff[];
 *	} BSDiff;
 *
 */

#include <stdio.h>

HERMES_CRITICAL bool applyDeltaPatch(const UpdateHeader * header, size_t currentIndex, size_t traceCounter, size_t previousCounter, bool dryRun)
{
	bool resuming = (dryRun || traceCounter < previousCounter), *pResuming = dryRun ? NULL : &resuming;

	const uint8_t * baseBSDiff = &((const uint8_t *) header)[sizeof(UpdateHeader) + currentIndex];

	//Check the flag to make sure we're properly aligned
	if(*(uint32_t*) baseBSDiff != BSDIFF_MAGIC)
		return false;

	baseBSDiff += sizeof(uint32_t);

	//Parsing the starting offset
	size_t currentPage = *(uint32_t*) baseBSDiff * BLOCK_SIZE;

	/*
	 * BaseBSDiff points to a compressed bytefield
	 * We will stream the decompression to cacheRAM, never using more than 4K of memory for our decompressed buffer
	 */

	BSDiffContext context = {
			//Setup the LZFX context ignoring the commands
			.lzfx = {
					.input = baseBSDiff + sizeof(uint32_t),
					.currentInputOffset = 0,
					.inputLength = header->sectionSignedDeviceKey.manifestLength - currentIndex - 2 * sizeof(uint32_t),

					.referenceOutput = cacheRAM,
					.output = cacheRAM,
					.outputRealSize = sizeof(cacheRAM),

					.status = LZFX_OK
			},
			.currentCacheOffset = 0,
			.lengthLeft = sizeof(cacheRAM),
			.isOutOfData = false
	};

	//We grab a good chunk of data
	if(lzfx_decompress(&context.lzfx, &context.lengthLeft) != LZFX_OK)
		return false;

	bool haveCachedPage = false, didDelta = false;
	uint8_t writeCounter = 0;
	uint16_t currentSegment = 0, currentOutputOffset = 0;
	uint32_t currentSegmentOffset = 0;

	/*
	 * From this point forward, we are streaming the BSDiff data structure in cacheRAM
	 * Because alignment will be all over the place, casting will be used liberally
	 */

	const uint32_t numberSegments = consumeDWord(&context);
	uint32_t currentSubsegmentLength = consumeDWord(&context);

	while(currentSegment < numberSegments && !context.isOutOfData)
	{
		//New page to patch!
		if(!haveCachedPage)
		{
			if((traceCounter & 1) == 0)
				incrementCounter(&traceCounter, previousCounter, pResuming);

			//Save a new page to cache
			if(!resuming)
				savePageToBuffer(currentPage, traceCounter);

			//Signal the patching is starting and the buffer page is filled
			incrementCounter(&traceCounter, previousCounter, pResuming);
			haveCachedPage = true;
			currentOutputOffset = 0;

			//Erase the old page
			if(!resuming)
				erasePage(currentPage);
		}

		//Actual patching

		//Insert
		const uint32_t lengthLeftSubSegment = currentSubsegmentLength - currentSegmentOffset;
		const uint16_t lengthLeftOutputPage = (const uint16_t) (BLOCK_SIZE - currentOutputOffset);
		uint32_t lengthLeft = MIN(lengthLeftSubSegment, lengthLeftOutputPage);

		currentSegmentOffset += lengthLeft;

		if(didDelta)
		{
			//Misaligned, we pad with a few bytes
			while(currentOutputOffset & 7u && lengthLeft)
			{
				uint8_t data = consumeByte(&context);

				if(!resuming)
					addByteToOutputBuffer(data, currentPage + currentOutputOffset, &writeCounter);

				currentOutputOffset += 1;
				lengthLeft -= 1;
			}

			//Perform the main copy
			while(lengthLeft >= sizeof(uint64_t))
			{
				uint64_t data = consumeQWord(&context);

				if(!resuming)
					writeToNAND(currentPage + currentOutputOffset, sizeof(data), (const uint8_t *) &data);

				currentOutputOffset += sizeof(data);
				lengthLeft -= sizeof(data);
			}

			//Finish up what may be left
			while(lengthLeft)
			{
				uint8_t data = consumeByte(&context);

				if(!resuming)
					addByteToOutputBuffer(data, currentPage + currentOutputOffset, &writeCounter);

				currentOutputOffset += 1;
				lengthLeft -= 1;
			}
		}
		//Patch
		else
		{
			const uint8_t * oldData = getBuffer(traceCounter - 1);

			//Misaligned, we pad with a few bytes
			while(currentOutputOffset & 7u && lengthLeft)
			{
				const uint8_t data = consumeByte(&context) + oldData[currentOutputOffset];

				if(!resuming)
					addByteToOutputBuffer(data, currentPage + currentOutputOffset, &writeCounter);

				currentOutputOffset += 1;
				lengthLeft -= 1;
			}

			//Perform the main copy
			while(lengthLeft >= sizeof(uint64_t))
			{
				union {
					uint64_t qword;
					uint8_t byte[8];
				} data;
				data.qword = consumeQWord(&context);

				data.byte[0] += oldData[currentOutputOffset];
				data.byte[1] += oldData[currentOutputOffset + 1];
				data.byte[2] += oldData[currentOutputOffset + 2];
				data.byte[3] += oldData[currentOutputOffset + 3];
				data.byte[4] += oldData[currentOutputOffset + 4];
				data.byte[5] += oldData[currentOutputOffset + 5];
				data.byte[6] += oldData[currentOutputOffset + 6];
				data.byte[7] += oldData[currentOutputOffset + 7];

				if(!resuming)
					writeToNAND(currentPage + currentOutputOffset, sizeof(data), data.byte);

				currentOutputOffset += sizeof(uint64_t);
				lengthLeft -= sizeof(uint64_t);
			}

			//Finish up what may be left
			while(lengthLeft)
			{
				const uint8_t data = consumeByte(&context) + oldData[currentOutputOffset];

				if(!resuming)
					addByteToOutputBuffer(data, currentPage + currentOutputOffset, &writeCounter);

				currentOutputOffset += 1;
				lengthLeft -= 1;
			}
		}

		//We finished a BSDiff subsegment
		if(currentSegmentOffset == currentSubsegmentLength)
		{
			//Not a subsegment, we actually finished a full segment
			if(didDelta)
				currentSegment += 1;

			didDelta = !didDelta;
			currentSegmentOffset = 0;

			//We shouldn't read past the end our section
			if(currentSegment < numberSegments)
				currentSubsegmentLength = consumeDWord(&context);
		}

		//We finished patching our current page
		if(currentOutputOffset == BLOCK_SIZE)
		{
			//Signal the patching is over
			incrementCounter(&traceCounter, previousCounter, pResuming);
			haveCachedPage = false;
		}
	}

	//We need to finish writing the current block, despite the end having been trimmed (also make sure we don't keep writing if we're having issues)
	if(currentOutputOffset != BLOCK_SIZE && !context.isOutOfData)
	{
		//Pad the current qword
		const uint8_t * oldData = getBuffer(traceCounter - 1);
		while(currentOutputOffset & WRITE_GRANULARITY_MASK)
		{
			const uint8_t data = oldData[currentOutputOffset];

			if(!resuming)
				addByteToOutputBuffer(data, currentPage + currentOutputOffset, &writeCounter);

			currentOutputOffset += 1;
		}

		if(!resuming)
			writeToNAND(currentPage + currentOutputOffset, BLOCK_SIZE - currentOutputOffset, &oldData[currentOutputOffset]);
	}

	return performValidation(&context, dryRun);
}
