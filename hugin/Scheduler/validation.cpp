//
// Created by Emile-Hugo Spir on 5/14/18.
//

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <ostream>
#include <cstring>
#include <vector>
#include <sys/param.h>

using namespace std;

#include "public_command.h"
#include "validation.h"
#include "Encoding/encoder.h"
#include "scheduler.h"
#include <crypto.h>

struct VerificationRangeCollector
{
	map<BlockID, DetailedBlock> data;

	void tag(size_t address, size_t length)
	{
		if(length == 0)
			return;

		const size_t spaceLeftInTag = BLOCK_SIZE - (address & BLOCK_OFFSET_MASK);
		const size_t lengthToTag = MIN(spaceLeftInTag, length);

		BlockID blockID(address);
		if(data.find(blockID) == data.end())
			data[blockID].segments.emplace_back(blockID);

		data[blockID].insertNewSegment(DetailedBlockMetadata(Address(address), lengthToTag, true));

		if(lengthToTag < length)
			tag(address + lengthToTag, length - lengthToTag);
	}
};

uint8_t * generateVirtualFlash(size_t & flashLength)
{
	//Craft the virtual flash
	if(flashLength & BLOCK_OFFSET_MASK)
	{
		flashLength += BLOCK_SIZE;
		flashLength &= BLOCK_MASK;
	}

	auto *virtualFlash = (uint8_t *) malloc(flashLength);
	if(virtualFlash != nullptr)
		memset(virtualFlash, 0xff, flashLength);

	return virtualFlash;
}

bool validateSchedulerPatch(const uint8_t * original, size_t originalLength, const uint8_t * newer, size_t newLength, const SchedulerPatch & patch)
{
	//We make sure the payload is properly encoded and decoded
	if(Encoder().validate(patch.commands) == 0)
	{
		cerr << "Couldn't validate the bytecode!" << endl;
		return false;
	}

	uint8_t *virtualFlash = nullptr;

	//Craft the virtual flash
	size_t flashLength = MAX(originalLength, newLength);
	virtualFlash = generateVirtualFlash(flashLength);
	if(virtualFlash == nullptr)
	{
		cerr << "Couldn't allocate memory for the virtual flash!" << endl;
		return false;
	}

	//Copy the old buffer to the "flash"
	memcpy(virtualFlash, original, originalLength);
	if(!virtualMachine(patch.commands, virtualFlash, flashLength))
	{
		cerr << "Preimage virtual machine error!" << endl;
		free(virtualFlash);
		return false;
	}

	//Execute the patch
	if(!executeBSDiffPatch(patch, virtualFlash, flashLength))
	{
		cerr << "BSDiff virtual machine error!" << endl;
		free(virtualFlash);
		return false;
	}

	//Check the result
	if(memcmp(virtualFlash, newer, newLength) != 0)
	{
		cerr << "Couldn't produce the proper final image!" << endl;

		FILE * flash = fopen("virtualFlash.bin", "wb");
		if(flash != nullptr)
		{
			fwrite(virtualFlash, 1, newLength, flash);
			fclose(flash);
		}

		dumpCommands(patch.commands, "commands.txt");

		free(virtualFlash);
		return false;
	}

	free(virtualFlash);
	return true;
}

void addReadRange(VerificationRangeCollector & readRange, const VerificationRangeCollector & writeRange, size_t baseAddress, size_t realLength)
{
	Address address(baseAddress);
	const size_t spaceLeftInTag = BLOCK_SIZE - (baseAddress & BLOCK_OFFSET_MASK);
	size_t length = MIN(spaceLeftInTag, realLength);

	auto blockRange = writeRange.data.find(address.getBlock());

	if(blockRange != writeRange.data.end())
	{
		Address endBase(address + length);
		const auto & segments = blockRange->second.segments;

		auto wRange = lower_bound(segments.begin(), segments.end(), address, [](const DetailedBlockMetadata & meta, const Address & add) { return (meta.source + meta.length) < add; });
		for(; wRange != segments.end(); ++wRange)
		{
			if(!wRange->tagged)
				continue;

			//Block start after we end. We can stop
			if(wRange->source > endBase)
				break;

			//The segment end before we start
			const Address endSegment(wRange->source + wRange->length);
			if(endSegment < address)
				continue;

			///This is _expensive_
			if(wRange->overlapWith(address, length))
			{
				//The intersecting segment start before the range we're trying to insert
				if(wRange->source <= address)
				{
					//No segment left
					if(endSegment >= endBase)
						return;

					//We shrink the segment and keep going
					address = endSegment;
					length = endBase.value - endSegment.value;
					endBase = address + length;
				}
				//The intersection starts within us. Does it finish after the end of our segment?
				else if(endSegment >= endBase)
				{
					//We shrink the segment and keep going
					length = endBase.value - wRange->source.value;
					endBase = address + length;
				}
				//This split our segment in two parts. Awesome.
				else
				{
					//We shrink the first half
					length = endBase.value - wRange->source.value;

					const Address secHalf(endSegment);
					const size_t secHalfLength = endBase.value - endSegment.value;

					//We make a recursive call with the smallest of the two halves
					if(length > secHalfLength)
					{
						addReadRange(readRange, writeRange, secHalf.value, secHalfLength);
					}
					else
					{
						addReadRange(readRange, writeRange, address.value, length);
						address = secHalf;
						length = secHalfLength;
					}

					endBase = address + length;
				}
			}
		}
	}

	if(length != 0)
		readRange.tag(address.value, length);

	//If we tried adding a range beyond the end of the block, we recursively call ourselves
	if(spaceLeftInTag < realLength)
		addReadRange(readRange, writeRange, baseAddress + spaceLeftInTag, realLength - spaceLeftInTag);
}

void generateVerificationRangesPrePatch(SchedulerPatch &patch, size_t initialOffset)
{
	//We need to collect all reads

	VerificationRangeCollector readRanges, writtenRanges;
	size_t endAddressCopy = 0;

	//Most of them will come from the bytecode
	//We want to ignore ranges we wrote ourselves ("fun")

	for(const auto & command : patch.commands)
	{
		switch(command.command)
		{
			case ERASE:
			case COMMIT:
			{
				writtenRanges.tag(command.mainAddress, BLOCK_SIZE);
				break;
			}
			case FLUSH_AND_PARTIAL_COMMIT:
			{
				writtenRanges.tag(command.mainAddress, command.length);
				break;
			}

			case LOAD_AND_FLUSH:
			{
				addReadRange(readRanges, writtenRanges, command.mainAddress, command.length);
				writtenRanges.tag(command.mainAddress, BLOCK_SIZE);
				break;
			}

			case COPY:
			{
				if(!isCache(command.mainAddress))
					addReadRange(readRanges, writtenRanges, command.mainAddress, command.length);

				if(!isCache(command.secondaryAddress))
					writtenRanges.tag(command.secondaryAddress, command.length);

				endAddressCopy = command.secondaryAddress + command.length;

				break;
			}
			case CHAINED_COPY:
			{
				if(!isCache(command.mainAddress))
					addReadRange(readRanges, writtenRanges, command.mainAddress, command.length);

				if(!isCache(endAddressCopy))
					writtenRanges.tag(endAddressCopy, command.length);

				endAddressCopy += command.length;
				break;
			}

			case CHAINED_COPY_SKIP:
			{
				endAddressCopy += command.length;
				break;
			}

			//Encoding
			case USE_BLOCK:break;
			case RELEASE_BLOCK:break;
			case REBASE:break;
			case END_OF_STREAM:break;
		}
	}

	//The rest will come from blocks identical before and in the preimage, but still read by the delta patch
	size_t readHeadBeforeExtra = initialOffset;
	for(const auto & bsdiff : patch.bsdiff)
	{
		addReadRange(readRanges, writtenRanges, initialOffset, bsdiff.delta.length);

		initialOffset += bsdiff.delta.length;
		readHeadBeforeExtra = initialOffset;

		initialOffset += bsdiff.extra.length;
	}

	//The last write was an delta, and was apparently trimmed. We complete the page
	if(readHeadBeforeExtra == initialOffset && initialOffset & BLOCK_OFFSET_MASK)
	{
		const size_t aditionnalLengthToCheck = BLOCK_SIZE - (initialOffset & BLOCK_OFFSET_MASK);
		addReadRange(readRanges, writtenRanges, initialOffset, aditionnalLengthToCheck);
	}

	//Compact, then add to the oldRange vector in small enough chunks. We start by counting the space we need to allocate in oldRanges
	size_t lengthToAllocate = 0;
	for(auto & readRange : readRanges.data)
	{
		readRange.second.compactSegments();
		for (const auto &segment : readRange.second.segments)
		{
			if (segment.tagged)
			{
				if (segment.length > VerificationRange::maxLength)
					lengthToAllocate += segment.length / VerificationRange::maxLength + (segment.length % VerificationRange::maxLength != 0);
				else
					lengthToAllocate += 1;
			}
		}
	}

	//Grab the memory
	patch.oldRanges.reserve(lengthToAllocate);

	//Actually fill the data in the buffer
	for(auto & readRange : readRanges.data)
	{
		for (const auto &segment : readRange.second.segments)
		{
			if (segment.tagged)
			{
				size_t patchLength = segment.length;
				initialOffset = segment.source.getAddress();

				while(patchLength)
				{
					const size_t rangeLength = (patchLength > VerificationRange::maxLength ? VerificationRange::maxLength : patchLength);

					patch.oldRanges.emplace_back(VerificationRange(static_cast<uint32_t>(initialOffset), rangeLength));

					patchLength -= rangeLength;
					initialOffset += rangeLength;
				}
			}
		}

	}
}

void generateVerificationRangesPostPatch(SchedulerPatch & patch, size_t initialOffset, const size_t fileLength)
{
	//Compute the sequential length we're writing to
	size_t patchLength = 0;
	for(const auto & bsdiff : patch.bsdiff)
		patchLength += bsdiff.delta.length + bsdiff.extra.length;

	//We trimmed the end of a block, we want to check it out (we'll cap to the size of the file a bit later)
	if(patchLength & BLOCK_OFFSET_MASK)
	{
		patchLength += BLOCK_SIZE - (patchLength & BLOCK_OFFSET_MASK);
		if(patchLength > fileLength)
			patchLength = fileLength;
	}

	patch.newRanges.clear();
	//Compute the ranges we want to have checked after the patch
	while(patchLength)
	{
		const size_t rangeLength = (patchLength > VerificationRange::maxLength ? VerificationRange::maxLength : patchLength);

		patch.newRanges.emplace_back(VerificationRange(static_cast<uint32_t>(initialOffset), rangeLength));

		patchLength -= rangeLength;
		initialOffset += rangeLength;
	}
}

void computeExpectedHashForRanges(vector<VerificationRange> &ranges, const uint8_t * data, size_t dataLength)
{
	for(auto & range : ranges)
	{
		assert(range.start + range.length <= dataLength);

		uint8_t hash[HASH_LENGTH];
		char hex[HASH_LENGTH * 2 + 1];

		hashMemory(&data[range.start], range.length, hash);

		hydro_bin2hex(hex, sizeof(hex), hash, sizeof(hash));

		range.expectedHash = string(hex);
	}
}
