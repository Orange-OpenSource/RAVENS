//
// Created by Emile-Hugo Spir on 3/29/18.
//

#include <vector>
#include <string>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <unistd.h>
#include "public_command.h"
#include <decoding/decoder_config.h>
#include "bsdiff/bsdiff.h"

using namespace std;

//The address must be block aligned and fit in memory
#define checkAligned(a) { assert(((a) & BLOCK_OFFSET_MASK) == 0); assert((a) < flashLength); }

#define STRICT_VM

#ifdef STRICT_VM
#define DEFAULT_NAND_VALUE 0xff
#else
#define DEFAULT_NAND_VALUE 0x0
#endif

//NAND writes on un-erased pages might work but have side effects we're trying to simulate
//The emulated NAND is a charge-trap design

void writeFlash(uint8_t * flash, uint8_t * source, size_t length)
{
	while(length--)
	{
#ifdef STRICT_VM
		if(*flash != 0xff)
			assert(false);
#endif
		*flash++ = *source++;
	}
}

void performCopy(uint8_t * flash, size_t flashLength, uint8_t * cache, size_t source, size_t length, size_t dest)
{
	assert(length < BLOCK_SIZE);	//Operations must fit within a block

	const bool mainIsCache = isCache(source);
	const bool secIsCache = isCache(dest);

	if(!mainIsCache)
	{
		assert(source < flashLength);
		assert(source + length <= flashLength);
	}
	else
		assert(isCache(source + length - 1));

	if(!secIsCache)
	{
		assert(dest < flashLength);
		assert(dest + length <= flashLength);
	}
	else
		assert(isCache(dest + length - 1));

	//Cache to cache
	if(mainIsCache && secIsCache)
	{
		memmove(&cache[dest & BLOCK_OFFSET_MASK], &cache[source & BLOCK_OFFSET_MASK], length);
	}
	else if(mainIsCache)
	{
		writeFlash(&flash[dest], &cache[source & BLOCK_OFFSET_MASK], length);
	}
	else if(secIsCache)
	{
		memcpy(&cache[dest & BLOCK_OFFSET_MASK], &flash[source], length);
	}
	else
	{
		writeFlash(&flash[dest], &flash[source], length);
	}
}

bool virtualMachine(const vector<PublicCommand> & commands, uint8_t * flash, size_t flashLength)
{
	if(flash == nullptr || flashLength == 0 || (flashLength & BLOCK_OFFSET_MASK) != 0)
		return false;

	bool previousWriteWasCopy = false;
	size_t endPreviousCopy = 0;

	//BLOCK_SIZE isn't necessarily constant
	uint8_t * cache = (uint8_t *) malloc(BLOCK_SIZE);

	if(cache == nullptr)
		return false;

	//Set the default value of the cache
	memset(cache, 0xff, BLOCK_SIZE);

	for(const PublicCommand command : commands)
	{
		bool isChainCompatibleOperation = false;

		switch(command.command)
		{
			case COMMIT:
			{
				checkAligned(command.mainAddress);
				memcpy(&flash[command.mainAddress], cache, BLOCK_SIZE);
				break;
			}

			case ERASE:
			{
				checkAligned(command.mainAddress);
				memset(&flash[command.mainAddress], DEFAULT_NAND_VALUE, BLOCK_SIZE);
				break;
			}

			case LOAD_AND_FLUSH:
			{
				checkAligned(command.mainAddress);
				memcpy(cache, &flash[command.mainAddress], BLOCK_SIZE);
				memset(&flash[command.mainAddress], DEFAULT_NAND_VALUE, BLOCK_SIZE);
				break;
			}

			case FLUSH_AND_PARTIAL_COMMIT:
			{
				checkAligned(command.mainAddress);
				assert(command.length <= BLOCK_SIZE);	//Operations must fit within a block

				isChainCompatibleOperation = true;
				endPreviousCopy = command.mainAddress + command.length;

				memset(&flash[command.mainAddress], DEFAULT_NAND_VALUE, BLOCK_SIZE);
				writeFlash(&flash[command.mainAddress], cache, command.length);
				break;
			}

			case CHAINED_COPY:
			{
				assert(previousWriteWasCopy);

				performCopy(flash, flashLength, cache, command.mainAddress, command.length, endPreviousCopy);

				isChainCompatibleOperation = true;
				endPreviousCopy += command.length;
				break;
			}

			case COPY:
			{
				performCopy(flash, flashLength, cache, command.mainAddress, command.length, command.secondaryAddress);

				isChainCompatibleOperation = true;
				endPreviousCopy = command.secondaryAddress + command.length;
				break;
			}

			case CHAINED_COPY_SKIP:
			{
				assert(command.length <= MAX_SKIP_LENGTH);

				if(isCache(endPreviousCopy))
				{
					assert(isCache(endPreviousCopy + command.length));
				}
				else
				{
					assert(endPreviousCopy < flashLength);
					assert(endPreviousCopy + command.length < flashLength);
				}

				isChainCompatibleOperation = true;
				endPreviousCopy += command.length;
				break;
			}

			//Decoding instructions
			case REBASE:
			case USE_BLOCK:
			case RELEASE_BLOCK:
			{
				isChainCompatibleOperation = true;
				break;
			}

			case END_OF_STREAM:
			{
				free(cache);
				return false;
			}
		}

		previousWriteWasCopy = isChainCompatibleOperation;
	}

	free(cache);
	return true;
}

bool executeBSDiffPatch(const SchedulerPatch & commands, uint8_t * flash, size_t flashLength)
{
	if(flash == nullptr || flashLength == 0 || (flashLength & BLOCK_OFFSET_MASK) != 0)
		return false;

	size_t currentPos = commands.startAddress << BLOCK_SIZE_BIT;
	for(const auto &patch : commands.bsdiff)
	{
		//Apply delta
		for(size_t i = 0; i < patch.delta.length; ++i)
			flash[currentPos++] += patch.delta.data[i];

		for(size_t i = 0; i < patch.extra.length; ++i)
			flash[currentPos++] = patch.extra.data[i];
	}

	return true;
}