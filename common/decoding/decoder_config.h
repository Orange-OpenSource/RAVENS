//
// Created by Emile-Hugo Spir on 4/25/18.
//

#ifndef SCHEDULER_DECODER_CONFIG_H
#define SCHEDULER_DECODER_CONFIG_H

/*
 * Instruction encoding table
 *
 *	+------+------------------------+-------------------------+--------------------+--------------------------+
 *	|      |          xx00          |          xx01           |        xx10        |           xx11           |
 *	+------+------------------------+-------------------------+--------------------+--------------------------+
 *	| 00xx | ERASE                  | LOAD_AND_FLUSH          | COMMIT             | FLUSH_AND_PARTIAL_COMMIT |
 *	| 01xx | USE_BLOCK              | RELEASE_BLOCK           | REBASE             |                          |
 *	| 10xx | COPY_NAND_TO_NAND      | COPY_NAND_TO_CACHE      | COPY_CACHE_TO_NAND | COPY_CACHE_TO_CACHE      |
 *	| 11xx | CHAINED_COPY_FROM_NAND | CHAINED_COPY_FROM_CACHE | CHAINED_COPY_SKIP  | END_OF_STREAM            |
 *	+------+------------------------+-------------------------+--------------------+--------------------------+
 *
 */

//Instructions are 4 bit wide
#define INSTRUCTION_WIDTH 4

//REBASE's second argument is the number of bits to write to cover the range of BlockID
///FIXME: Make dynamic, hopefully something like numberOfBitsNecessary(BLOCK_ID_SPACE)
#define REBASE_LENGTH_BITS 4

typedef enum
{
	OPCODE_ERASE = 0b0000,
	OPCODE_LOAD_FLUSH = 0b0001,
	OPCODE_COMMIT = 0b0010,
	OPCODE_FLUSH_COMMIT = 0b0011,
	OPCODE_USE_BLOCK = 0b0100,
	OPCODE_RELEASE = 0b0101,
	OPCODE_REBASE = 0b0110,

	OPCODE_ILLEGAL = 0b0111,

	OPCODE_COPY_NN = 0b1000,
	OPCODE_COPY_NC = 0b1001,
	OPCODE_COPY_CN = 0b1010,
	OPCODE_COPY_CC = 0b1011,
	OPCODE_CHAINED_COPY_N = 0b1100,
	OPCODE_CHAINED_COPY_C = 0b1101,
	OPCODE_CHAINED_SKIP = 0b1110,
	OPCODE_END_OF_STREAM = 0b1111
} OPCODE;

//Encoder related config
#define MAX_SKIP_LENGTH_BITS 6u

#endif //SCHEDULER_DECODER_CONFIG_H
