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

#include <cstdlib>
#include <cstdint>
#include <vector>
#include <sys/param.h>
#include <cstring>
#include <cassert>
#include <iostream>

#include "public_command.h"
#include "bsdiff/bsdiff.h"

using namespace std;

bool validateBSDiff(const uint8_t * original, size_t originalLength, const uint8_t * newer, size_t newLength, const vector<BSDiffPatch> & patch, size_t earlySkip)
{
	size_t flashLength = MAX(originalLength, newLength);
	auto *virtualFlash = (uint8_t*) calloc(flashLength, sizeof(uint8_t));
	size_t currentOffset = earlySkip;

	if(virtualFlash == nullptr)
	{
		cerr << "Memory error in BSDiff validation" << endl;
		return false;
	}

	memcpy(virtualFlash, original, originalLength);

#ifdef PRINT_BSDIFF
	FILE * file = fopen("bsdiff.txt", "w+");
	assert(file != nullptr);
#endif

	for(auto cur : patch)
	{
#ifdef PRINT_BSDIFF
		fprintf(file, "[0x%zx] Copying %zu from 0x%zx then adding %zu new bytes\n", currentOffset, cur.lengthDelta, cur.oldDataAddress, cur.lengthExtra);
#endif
		if(cur.lengthDelta)
		{
			assert(cur.oldDataAddress + cur.lengthDelta <= originalLength);
			memcpy(&virtualFlash[currentOffset], &original[cur.oldDataAddress], cur.lengthDelta);
			for(size_t i = 0; i < cur.lengthDelta; ++i)
				virtualFlash[currentOffset++] += cur.deltaData[i];
		}

		for(size_t i = 0; i < cur.lengthExtra; ++i)
			virtualFlash[currentOffset++] = newer[cur.extraPos + i];

		assert(currentOffset <= flashLength);
	}

#ifdef PRINT_BSDIFF
	fclose(file);
#endif

	bool output = memcmp(virtualFlash, newer, newLength) == 0;

	if(!output)
	{
		FILE * flash = fopen("virtualFlash.bin", "wb");
		if(flash != nullptr)
		{
			fwrite(virtualFlash, 1, newLength, flash);
			fclose(flash);
		}
	}

	free(virtualFlash);

	return output;
}
