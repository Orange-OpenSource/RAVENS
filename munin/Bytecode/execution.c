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
#include "execution.h"
#include "../core.h"
#include <memory.h>

typedef struct
{
	bool isCache : 1;
	size_t chainAddress : 31;
} ChainAddress;

static uint8_t writeCache[WRITE_GRANULARITY] = {0};
static uint8_t currentWriteCachePos = 0;
static size_t prevDest = 0;

RAVENS_CRITICAL void performCopyWithCache(size_t dest, const uint8_t * source, size_t length)
{
	//The previous write was misaligned
	if(currentWriteCachePos != 0)
	{
		//Next write doesn't follow the previous one, we need to flush the cache
		if(dest <= prevDest || prevDest + WRITE_GRANULARITY <= dest)
		{
			writeToNAND(dest, currentWriteCachePos, writeCache);
			currentWriteCachePos = 0;
		}
		else
		{
			//The next write isn't stricly following the previous one. We pad the write cache
			if(prevDest + currentWriteCachePos != dest)
			{
				memset(&writeCache[currentWriteCachePos], 0xff, WRITE_GRANULARITY - (dest & WRITE_GRANULARITY_MASK));
				currentWriteCachePos += dest & WRITE_GRANULARITY_MASK;
			}

			const uint8_t lengthLeft = (const uint8_t) (WRITE_GRANULARITY - currentWriteCachePos);

			//don't have enough data to fill the buffer
			if(length < lengthLeft)
			{
				memcpy(&writeCache[currentWriteCachePos], source, length);
				currentWriteCachePos += length;
				return;
			}
			else
			{
				memcpy(&writeCache[currentWriteCachePos], source, lengthLeft);
				writeToNAND(prevDest, WRITE_GRANULARITY, writeCache);

				//Move the read/write head forward
				source += lengthLeft;
				dest += lengthLeft;
				length -= lengthLeft;

				currentWriteCachePos = 0;
			}
		}
	}

	//Perform the aligned write
	writeToNAND(dest, length & ~WRITE_GRANULARITY_MASK, source);

	//The end of the write isn't aligned
	if(length & WRITE_GRANULARITY_MASK)
	{
		const size_t lengthWritten = length & ~WRITE_GRANULARITY_MASK;

		currentWriteCachePos = (uint8_t) (length - lengthWritten);
		memcpy(writeCache, &source[lengthWritten], currentWriteCachePos);
		prevDest = dest + lengthWritten;
	}
}

RAVENS_CRITICAL void flushCopyCache()
{
	if(currentWriteCachePos != 0)
	{
		memset(&writeCache[currentWriteCachePos], 0xff, WRITE_GRANULARITY - currentWriteCachePos);
		writeToNAND(prevDest, WRITE_GRANULARITY, writeCache);
		currentWriteCachePos = 0;
	}
}

RAVENS_CRITICAL void performCopy(const DecodedCommand decodedCommand)
{
	if(decodedCommand.command == OPCODE_COPY_CC)
	{
		memmove(&cacheRAM[decodedCommand.secondaryAddress], &cacheRAM[decodedCommand.mainAddress], decodedCommand.length);
	}
	else if(decodedCommand.command == OPCODE_COPY_CN)
	{
		performCopyWithCache(decodedCommand.secondaryAddress, &cacheRAM[decodedCommand.mainAddress], decodedCommand.length);
	}
	else if(decodedCommand.command == OPCODE_COPY_NC)
	{
		memcpy(&cacheRAM[decodedCommand.secondaryAddress], (uint8_t *) decodedCommand.mainAddress, decodedCommand.length);
	}
	else if(decodedCommand.command == OPCODE_COPY_NN)
	{
		performCopyWithCache(decodedCommand.secondaryAddress, (const uint8_t *) decodedCommand.mainAddress, decodedCommand.length);
	}
}

RAVENS_CRITICAL bool processInstruction(const DecodedCommand decodedCommand, size_t * stepCount, ChainAddress *chainAddress, bool * fastForward)
{
	switch(decodedCommand.command)
	{
		//Virtual instructions
		case OPCODE_FLUSH_COMMIT:
		{
			const DecodedCommand newCommand = {.command = OPCODE_ERASE,
					.mainAddress = decodedCommand.mainAddress,
					.secondaryAddress = 0,
					.length = 0};

			if(!processInstruction(newCommand, stepCount, chainAddress, fastForward))
			{
				return false;
			}

			//Copy from the cache to the beginning of the destination block for the determined length
			const DecodedCommand newCommand2 = {.command = OPCODE_COPY_CN,
					.mainAddress = 0,
					.secondaryAddress = decodedCommand.mainAddress,
					.length = decodedCommand.length};

			if(!processInstruction(newCommand2, stepCount, chainAddress, fastForward))
			{
				return false;
			}

			break;
		}

		case OPCODE_LOAD_FLUSH:
		{
			//Load the content of the page
			const DecodedCommand newCommand = {.command = OPCODE_COPY_NC,
					.mainAddress = decodedCommand.mainAddress,
					.secondaryAddress = 0,
					.length = BLOCK_SIZE};

			if(!processInstruction(newCommand, stepCount, chainAddress, fastForward))
			{
				return false;
			}

			//Erase the page
			const DecodedCommand newCommand2 = {.command = OPCODE_ERASE,
					.mainAddress = decodedCommand.mainAddress,
					.secondaryAddress = 0,
					.length = 0};

			if(!processInstruction(newCommand2, stepCount, chainAddress, fastForward))
			{
				return false;
			}

			break;
		}

		case OPCODE_COMMIT:
		{
			const DecodedCommand newCommand = {.command = OPCODE_COPY_CN,
					.mainAddress = 0,
					.secondaryAddress = decodedCommand.mainAddress,
					.length = BLOCK_SIZE};

			if(!processInstruction(newCommand, stepCount, chainAddress, fastForward))
			{
				return false;
			}
			break;
		}

		case OPCODE_CHAINED_COPY_C:
		case OPCODE_CHAINED_COPY_N:
		{
			OPCODE opcode;
			if(decodedCommand.command == OPCODE_CHAINED_COPY_C)
			{
				if(chainAddress->isCache)
					opcode = OPCODE_COPY_CC;
				else
					opcode = OPCODE_COPY_CN;
			}
			else
			{
				if(chainAddress->isCache)
					opcode = OPCODE_COPY_NC;
				else
					opcode = OPCODE_COPY_NN;
			}

			const DecodedCommand newCommand = {.command = opcode,
					.mainAddress = decodedCommand.mainAddress,
					.secondaryAddress = chainAddress->chainAddress,
					.length = decodedCommand.length};

			if(!processInstruction(newCommand, stepCount, chainAddress, fastForward))
			{
				return false;
			}

			break;
		}

		case OPCODE_CHAINED_SKIP:
		{
			chainAddress->chainAddress += decodedCommand.length;
			break;
		}

		//Operations requiring a backup of the cache before execution
		case OPCODE_ERASE:
		{
			//Dry run
			if(stepCount == NULL)
				break;

			flushCopyCache();

			const size_t oldCounter = getCurrentCounter();

			//Increase the counter signaling we're about to back up our cache
			incrementCounter(stepCount, oldCounter, fastForward);

			//Backup the cache if not fast forwarding
			if(!*fastForward)
				backupCache(*stepCount);

			//Increase the counter signaling we backed up our cache before erasing something
			incrementCounter(stepCount, oldCounter, fastForward);

			if(!*fastForward)
				erasePage(decodedCommand.mainAddress);

			break;
		}

		case OPCODE_COPY_NC:
		case OPCODE_COPY_CC:
		{
			chainAddress->isCache = true;
			chainAddress->chainAddress = decodedCommand.secondaryAddress + decodedCommand.length;

			//Dry run/fast forwarding
			if(stepCount != NULL && !*fastForward)
				performCopy(decodedCommand);

			break;
		}
		case OPCODE_COPY_NN:
		case OPCODE_COPY_CN:
		{
			chainAddress->isCache = false;
			chainAddress->chainAddress = decodedCommand.secondaryAddress + decodedCommand.length;

			//Dry run/fast forwarding
			if(stepCount != NULL && !*fastForward)
				performCopy(decodedCommand);

			break;
		}

		//Instructions not to execute
		case OPCODE_USE_BLOCK:
		case OPCODE_RELEASE:
		case OPCODE_REBASE:
		{
			break;
		}

		case OPCODE_END_OF_STREAM:
		case OPCODE_ILLEGAL:
		{
			return false;
		}
	}

	return true;
}

RAVENS_CRITICAL bool runCommands(const uint8_t * bytes, size_t * currentByteOffset, size_t length, size_t *currentTrace, size_t oldCounter, bool dryRun)
{
	DecoderContext decoderContext = {
			.usingBlock = false,
			.blockInUse = 0,
			.blockIDBits = BLOCK_ID_SPACE,
			.blockBase = BLOCK_ID_SPACE,
			.blockIDBitsRef = BLOCK_ID_SPACE,
			.blockSizeBitsRef = BLOCK_SIZE_BIT
	};

	//We first do a dry run to make sure all operations will properly decode.
	//In this cause, we communicate no writes shall be performed
	bool needFastForwarding = !dryRun && oldCounter != 0;
	bool *pNeedFastForwarding = dryRun ? NULL : &needFastForwarding;

	if(needFastForwarding)
		restoreCache(oldCounter);

	ChainAddress chainAddress;
	DecodedCommand decodedCommand;

	//Perform the update
	while(decodeInstruction(&decoderContext, bytes, currentByteOffset, length, &decodedCommand) && decodedCommand.command != OPCODE_END_OF_STREAM)
	{
		//Something is really wrong: we decoded an illegal instruction!
		if(!processInstruction(decodedCommand, currentTrace, &chainAddress, pNeedFastForwarding))
			return false;
	}

	//CurrentByteOffset was used as currentBitOffset. We need to patch it up
	if(*currentByteOffset & 0x7u)
		*currentByteOffset += 8;

	//Divide by 8
	*currentByteOffset >>= 3;

	return true;
}