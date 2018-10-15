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

#ifndef HERMES_PUBLIC_COMMAND_H
#define HERMES_PUBLIC_COMMAND_H

#include "config.h"

enum INSTR
{
	ERASE,

	COMMIT,
	FLUSH_AND_PARTIAL_COMMIT,

	LOAD_AND_FLUSH,

	COPY,
	CHAINED_COPY,
	CHAINED_COPY_SKIP,

	/*
	 * Those operations are optimizations used by the encoder.
	 * They let the encoder tell the decoder that until another USE/RELEASE_BLOCK is met, the first block of future operations is to be decoded as the provided block
	 * The exception is of course copies from the cache as they are already encoded differently. In this case, the secondary operand is affected. Cache to cache copies are ignored
	 * For block to block copies, only the first operand is affected
	 */
	USE_BLOCK,
	RELEASE_BLOCK,

	/*
	 * This operation is an optimisation used by the encoder
	 * It tells the encoder and the decode the range of blocks that is about to be used, and let us encode BlockID in as few bits as possible
	 */
	REBASE,

	END_OF_STREAM
};

#define CACHE_ADDRESS (SIZE_MAX & ~((BLOCK_SIZE << 1u) - 1u))
#define MAX_SKIP_LENGTH ((1u << MAX_SKIP_LENGTH_BITS) - 1)
#define isCache(a) ((a) >= CACHE_ADDRESS && (a) <= (CACHE_ADDRESS + (BLOCK_SIZE - 1)))

struct BSDiffMoves
{
	size_t start, length, dest;

	BSDiffMoves(const size_t & start, const size_t & length, const size_t & dest) : start(start), length(length), dest(dest) {}
};

struct PublicCommand
{
	INSTR command;
	size_t mainAddress;
	size_t secondaryAddress;

	size_t length;
};

struct BSDiff
{
	struct DynamicArray
	{
		uint8_t * data;
		size_t length;

		void freeData();
	};

	DynamicArray delta;
	DynamicArray extra;
};


struct VerificationRange
{
	uint32_t start;
	uint16_t length;

	std::string expectedHash;

	VerificationRange(uint32_t _start, uint16_t _length) : start(_start), length(_length), expectedHash() {}

	static const size_t maxLength = (1u << (sizeof(VerificationRange::length) * 8)) - 1;
};

struct SchedulerPatch
{
	size_t startAddress;

	std::vector<PublicCommand> commands;
	std::vector<BSDiff> bsdiff;

	std::vector<VerificationRange> oldRanges;
	std::vector<VerificationRange> newRanges;

	void clear(bool withFree)
	{
		if(withFree)
		{
			for(auto diff : bsdiff)
			{
				diff.delta.freeData();
				diff.extra.freeData();
			}
		}

		bsdiff.clear();
		oldRanges.clear();
		newRanges.clear();
		startAddress = 0;
	}

	void compactBSDiff();
};


void schedule(const std::vector<BSDiffMoves> & input, std::vector<PublicCommand> & output, bool printStats = false);
bool generatePatch(const uint8_t *original, size_t originalLength, const uint8_t *newer, size_t newLength, SchedulerPatch &outputPatch, bool printStats);

bool runDynamicTestWithFiles(const char * original, const char * newFile);
bool virtualMachine(const std::vector<PublicCommand> & commands, uint8_t * flash, size_t flashLength);

void dumpCommands(const std::vector<PublicCommand> & commands, const char *path = nullptr);

#endif //HERMES_PUBLIC_COMMAND_H
