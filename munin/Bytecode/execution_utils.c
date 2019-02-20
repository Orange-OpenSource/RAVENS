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
#include "../core.h"
#include "../driver_api.h"
#include "../../common/layout.h"

#include <stdio.h>

extern const UpdateMetadata updateMetadataMain;
extern const UpdateMetadata updateMetadataSec;

extern const uint8_t backupCache1[BLOCK_SIZE];
extern const uint8_t backupCache2[BLOCK_SIZE];

/*
 * Counter behavior:
 *
 * 		At this point, the counter is incremented once after saving the cache, and once after erasing a page (which trigger a backup of the cache just prior)
 * 		Hence, the handling of counter is reverted between backup and restore (as the counter indicate the next cache to write to)
 *
 * 		The weakest bit isn't relevant in the choice of the space in which we will save the cache
 * 		If the next weakest is 1, we save to the second space and read from the first. If 0, vice versa.
 *
 */

void backupCache(size_t counter)
{
	if(counter & 2u)
	{
		erasePage((size_t) &backupCache2);
		writeToNAND((size_t) &backupCache2, BLOCK_SIZE, cacheRAM);
	}
	else
	{
		erasePage((size_t) &backupCache1);
		writeToNAND((size_t) &backupCache1, BLOCK_SIZE, cacheRAM);
	}
}

RAVENS_CRITICAL void restoreCache(size_t counter)
{
	if(counter & 2u)
		memcpy(cacheRAM, backupCache1, BLOCK_SIZE);
	else
		memcpy(cacheRAM, backupCache2, BLOCK_SIZE);
}

RAVENS_CRITICAL void setMetadataPage(const UpdateMetadata * currentMetadata, const UpdateHeader * updateLocation, uint32_t multiplier)
{
	size_t newMetadata;

	//Find the pointer to the new field
	if(currentMetadata == &updateMetadataMain)
	{
		newMetadata = (size_t) &updateMetadataSec;
	}
	else
	{
		newMetadata = (size_t) &updateMetadataMain;
	}

	//Erase the backup page
	erasePage(newMetadata);

	//Write back the location
	writeToNAND(newMetadata, sizeof(currentMetadata->location), (const uint8_t *) &updateLocation);

	//Ignore the bitfield as we want to keep it to its default, erased value
	newMetadata += sizeof(currentMetadata->location) + sizeof(currentMetadata->bitField);

	UpdateMetadataFooter footer = {
			.multiplier = multiplier,
			.valid = DEFAULT_64B_FLASH_VALUE,
			.notExpired = DEFAULT_64B_FLASH_VALUE};

	writeToNAND(newMetadata, sizeof(uint32_t), (const uint8_t *) &multiplier);

	//Only make the segment valid when we're sure everything was written properly (might be worth checking?)
	footer.valid = VALID_64B_VALUE;
	writeToNAND(newMetadata + ((size_t) &footer.valid - (size_t) &footer), sizeof(footer.valid), (const uint8_t *) &footer.valid);

	//Discard the current page
	footer.notExpired = 0;
	writeToNAND((size_t) &currentMetadata->footer.notExpired, sizeof(footer.notExpired), (const uint8_t *) &footer.notExpired);
}

RAVENS_CRITICAL void resetMetadataPage(const UpdateMetadata * currentMetadata, uint32_t multiplier)
{
	return setMetadataPage(currentMetadata, currentMetadata->location, multiplier);
}

#if MIN_BIT_WRITE_SIZE & 0x7u
	#define CURRENT_COUNTER_WIDTH ((MIN_BIT_WRITE_SIZE + 7) / 8)
#else
	#define CURRENT_COUNTER_WIDTH (MIN_BIT_WRITE_SIZE / 8)
#endif

#define USABLE_BIT_FIELD_COUNTER (sizeof(((UpdateMetadata *) 0)->bitField) / CURRENT_COUNTER_WIDTH)

RAVENS_CRITICAL void incrementCounter(size_t *counter, size_t oldCounter, bool *fastForward)
{
	*counter += 1;

	//If we are fast forwarding, we don't actually perform most of the logic
	if(fastForward == NULL || !*fastForward)
	{
		//Okay, let's determine what kind of write we have to perform
		volatile const UpdateMetadata * oldMetadata = getMetadata();

		//Are we overflowing the bitfield
		if((*counter - 1) / USABLE_BIT_FIELD_COUNTER != *counter / USABLE_BIT_FIELD_COUNTER)
		{
			//We need to reset the bitfield and increment the multiplier
			resetMetadataPage((const UpdateMetadata *) oldMetadata, oldMetadata->footer.multiplier + 1);
		}
		else
		{
			const uint8_t * bytesToWrite = (const uint8_t *) &oldMetadata->bitField[(*counter - 1) * CURRENT_COUNTER_WIDTH];

			uint8_t bytes[CURRENT_COUNTER_WIDTH] = {0};

			//Unset the new bits
			writeToNAND((size_t) bytesToWrite, CURRENT_COUNTER_WIDTH, bytes);
		}
	}
	else if(*counter == oldCounter)
	{
		//Have we hit the end of the fast forwarding?
		*fastForward = false;
	}
}

RAVENS_CRITICAL size_t getCurrentCounter()
{
	volatile const UpdateMetadata * metadata = getMetadata();
	assert(isMetadataValid(*metadata));

	size_t output = metadata->footer.multiplier * (sizeof(metadata->bitField) / CURRENT_COUNTER_WIDTH);

	for(size_t i = 0; i + CURRENT_COUNTER_WIDTH < sizeof(metadata->bitField); i += CURRENT_COUNTER_WIDTH)
	{
		uint8_t posCurChunk = 0;
		for(; posCurChunk < CURRENT_COUNTER_WIDTH; ++posCurChunk)
		{
			const uint8_t byte = metadata->bitField[i + posCurChunk];

			if(byte != 0)
				break;
		}
		if(posCurChunk == CURRENT_COUNTER_WIDTH)
			output += 1;
		else
			break;
	}

	return output;
}

RAVENS_CRITICAL bool writeToNAND(size_t address, size_t length, const uint8_t * source)
{
	//If misaligned, we keep the old data before our insertion point
	uint8_t misalignment = (uint8_t) (address & WRITE_GRANULARITY_MASK);
	if(misalignment != 0)
	{
		if(misalignment & ADDRESSING_GRANULARITY_MASK)
			return false;

		uint8_t missing[WRITE_GRANULARITY];
		const size_t lengthToCopy = MIN(WRITE_GRANULARITY - misalignment, length);

		//We have to pad before and after :( Therefore, to simplify the logic, we over-read a little
		if(lengthToCopy == length)
			memcpy(&missing, (const void *) (address - misalignment), WRITE_GRANULARITY);
		else
			memcpy(&missing, (const void *) (address - misalignment), WRITE_GRANULARITY - lengthToCopy);

		//Copy a little bit of extra data
		memset(&missing[misalignment], 0xff, lengthToCopy);

		//Perform the NAND write
		writeToNAND(address - misalignment, WRITE_GRANULARITY, missing);

		if(lengthToCopy == length)
			return true;

		length -= lengthToCopy;
		source += lengthToCopy;
		address += lengthToCopy;
	}

	const size_t lengthToWrite = length & ~WRITE_GRANULARITY_MASK;
	if(lengthToWrite)
		programFlash(address, source, lengthToWrite);

	//If we have some extra bytes at the end
	size_t finalMisalignment = length - lengthToWrite;
	if(finalMisalignment)
	{
		uint8_t missing[WRITE_GRANULARITY];

		//Copy final data from source
		memcpy(missing, &source[lengthToWrite], finalMisalignment);

		//Complete with old data
		memset(&missing[finalMisalignment], 0xff, WRITE_GRANULARITY - finalMisalignment);

		//Perform the NAND write
		writeToNAND(address + lengthToWrite, WRITE_GRANULARITY, missing);
	}

	return true;
}

RAVENS_CRITICAL void erasePage(size_t address)
{
	assert((address & BLOCK_OFFSET_MASK) == 0);
	eraseSector(address);
}
