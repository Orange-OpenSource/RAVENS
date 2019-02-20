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

#ifndef RAVENS_IO_MANAGEMENT_H
#define RAVENS_IO_MANAGEMENT_H

#include <stdbool.h>
#include "device/device_config.h"

#define STATIC_BIT_SIZE

#include "../common/decoding/decoder_config.h"

#ifndef MIN_BIT_WRITE_SIZE
	#define MIN_BIT_WRITE_SIZE (8u * WRITE_GRANULARITY)
#endif

#define WRITE_GRANULARITY_MASK (WRITE_GRANULARITY - 1u)
#define ADDRESSING_GRANULARITY_MASK (ADDRESSING_GRANULARITY - 1u)

#define BLOCK_SIZE 	(1u << BLOCK_SIZE_BIT)
#define FLASH_SIZE 	(1u << FLASH_SIZE_BIT)
#define BLOCK_ID_SPACE	(FLASH_SIZE_BIT - BLOCK_SIZE_BIT)
#define BLOCK_OFFSET_MASK (BLOCK_SIZE - 1)
#define BLOCK_MASK			(~BLOCK_OFFSET_MASK)

extern uint8_t cacheRAM[BLOCK_SIZE];

bool writeToNAND(size_t address, size_t length, const uint8_t * source);
void erasePage(size_t address);

#endif //RAVENS_IO_MANAGEMENT_H
