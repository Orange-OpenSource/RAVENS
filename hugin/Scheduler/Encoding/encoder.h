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

#ifndef SCHEDULER_ENCODER_H
#define SCHEDULER_ENCODER_H

#include "../public_command.h"
#include "../Address.h"

uint8_t numberOfBitsNecessary(size_t x);

class Encoder
{
	bool usingBlock;
	BlockID blockInUse;

	//How many blocks are available in the space (1 << blockIDBits) - 1
	uint8_t blockIDBits;
	BlockID blockBase;

	void writeBits(const uint64_t & bitsToWrite, const uint8_t & lengthToWrite, std::vector<uint64_t> & bitField, uint8_t & bitWidth) const;

	uint64_t extractBlockID(const uint64_t & address) const;

	void encodeInstruction(const PublicCommand & command, uint8_t & bitLength, std::vector<uint64_t> & instruction);
	void appendInstruction(const PublicCommand & command, uint8_t & currentByte, uint8_t & spaceLeftInByte, std::vector<uint8_t> &byteStream);
	bool _decodeInstruction(const uint8_t * byteStream, size_t & currentByteOffset, size_t length, PublicCommand & command);

	void reset();

public:

	void encode(const std::vector<PublicCommand> & commands, uint8_t* & byteField, size_t & length);
	void decode(const uint8_t * byteField, size_t length, std::vector<PublicCommand> & commands);

	size_t validate(const std::vector<PublicCommand> & commands);

	Encoder() : blockIDBits(BLOCK_ID_SPACE), usingBlock(false), blockInUse(0), blockBase(0) {}
};

#endif //SCHEDULER_ENCODER_H
