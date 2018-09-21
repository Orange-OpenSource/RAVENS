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

#ifndef SCHEDULER_DECODER_H
#define SCHEDULER_DECODER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "decoder_config.h"

typedef struct
{
	OPCODE command;
	size_t mainAddress;
	size_t secondaryAddress;

	size_t length;
} DecodedCommand;

typedef struct
{
	bool usingBlock;
	size_t blockInUse;

	//How many blocks are available in the space (1 << blockIDBits) - 1
	uint8_t blockIDBits;
	size_t blockBase;

	uint8_t blockIDBitsRef;
	uint8_t blockSizeBitsRef;

} DecoderContext;

#define MASK_OF_WIDTH(a) ((1u << (a)) - 1u)

bool decodeInstruction(DecoderContext * context, const uint8_t * byteStream, size_t * currentByteOffset, const size_t length, DecodedCommand * command);

#ifdef __cplusplus
}
#endif

#endif //SCHEDULER_DECODER_H
