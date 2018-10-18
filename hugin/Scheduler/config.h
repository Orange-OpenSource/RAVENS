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

#ifndef HERMES_CONFIG_H
#define HERMES_CONFIG_H

/*
 * Optimizations:
 *
 * Our main benchmark was forthPassTest() which is a complex network to resolve.
 * The metric we're trying to optimize in the number of operations (a side effect is to reudce the number of COPY which are very large opcodes)
 * Similar improvements were noticed in our other tests.
 *
 * 	Initial instructions #				: 59
 * 	+CODEGEN_OPTIMIZATIONS				: 48 (extremely efficient on benchmarks, likely less so in real world scenarios)
 *	+AVOID_UNECESSARY_REWRITE			: 41
 *	+IGNORE_BLOCK_LAYOUT_UNLESS_FINAL	: 39
 *	+IGNORE_CACHE_LAYOUT				: 39 (useful for forthPassTestWithCompetitiveRead)
 *
 *	Each optimization can be enabled or disabled individually and aren't dependant of each other unless #ifdefed
 */

//Look at the code generated to group commands in similar commands but fewer of them
#define CODEGEN_OPTIMIZATIONS

//Will try to avoid writting a block just to wipe it without using its content
//	Basically, we try to play Domino with some types of writes
#define AVOID_UNECESSARY_REWRITE

//Will layout blocks so that it minimize the number of write (at the cost of reads) unless we're writting the block final form
#define IGNORE_BLOCK_LAYOUT_UNLESS_FINAL

//Try to load data to the cache sequentially in order to enable instruction merging
#define IGNORE_CACHE_LAYOUT

/*
 * Various other options
 */

// #define WANT_DEBUG
#ifdef WANT_DEBUG
	#define VERBOSE_STATIC_TESTS
	#define PRINT_REAL_INSTRUCTIONS
//	#define PRINT_BSDIFF
	#define PRINT_SELECTED_LINKS
//	#define DISABLE_CHAINED_COPY
#endif

#define PRINT_SPEED

//Significant performance penalty, ≈ 10% on conflict resolution and the generation of conflict ranges
#define VERY_AGGRESSIVE_ASSERT

//Encoder related config
#define FLASH_SIZE_BIT_DEFAULT	20u		//How many bits should be used to encode addresses
#define BLOCK_SIZE_BIT_DEFAULT	12u		// 4096, 0x1000

extern size_t _realBlockSizeBit;
extern size_t _realFullAddressSpace;

#define BLOCK_SIZE_BIT ((const uint8_t) _realBlockSizeBit)
#define FLASH_SIZE_BIT ((const uint8_t) _realFullAddressSpace)

//Need to be usable as a masks
#define BLOCK_SIZE 			(1u << BLOCK_SIZE_BIT)
#define BLOCK_OFFSET_MASK	(BLOCK_SIZE - 1)
#define BLOCK_MASK			(~BLOCK_OFFSET_MASK)
#define BLOCK_ID_SPACE		((const uint8_t)(FLASH_SIZE_BIT - BLOCK_SIZE_BIT))

//How many uses of a BlockID warrant the overhead of a BLOCK_USE?
#define BLOCK_USE_THRESHOLD 3

#endif //HERMES_CONFIG_H
