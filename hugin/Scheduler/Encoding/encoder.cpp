//
// Created by Emile-Hugo Spir on 4/9/18.
//

#include <vector>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <string>
#include <unistd.h>
#include <cstdlib>
#include "encoder.h"
#include <decoder.h>
#include <sys/param.h>

void Encoder::writeBits(const uint64_t & bitsToWrite, const uint8_t & lengthToWrite, std::vector<uint64_t> & bitField, uint8_t & bitWidth) const
{
	if(lengthToWrite == 0)
		return;

	assert(bitWidth + lengthToWrite <= UINT8_MAX);

	//Will this instruction make us use a new qword?
	const uint8_t nbBitsInQword = 8 * sizeof(uint64_t);
	const uint8_t spaceLeft = nbBitsInQword - (bitWidth % nbBitsInQword);

	if(lengthToWrite <= spaceLeft)
	{
		uint64_t & qword = bitField.back();
		qword <<= lengthToWrite;
		qword |= bitsToWrite & MASK_OF_WIDTH(lengthToWrite);
		bitWidth += lengthToWrite;
	}
	else
	{
		const uint64_t bitsToWriteInFirstBatch = bitsToWrite >> (lengthToWrite - spaceLeft);
		writeBits(bitsToWriteInFirstBatch, spaceLeft, bitField, bitWidth);

		bitField.push_back(0);
		writeBits(bitsToWrite, lengthToWrite - spaceLeft, bitField, bitWidth);
	}
}

uint64_t Encoder::extractBlockID(const uint64_t & address) const
{
	uint64_t output = (address - blockBase.value) >> BLOCK_SIZE_BIT;
	assert(numberOfBitsNecessary(output) <= blockIDBits);
	return output;
}

void Encoder::encodeInstruction(const PublicCommand & command, uint8_t & bitLength, std::vector<uint64_t> & instruction)
{
	instruction.clear();
	instruction.push_back(0);

	bitLength = 0;

	switch (command.command)
	{
		case ERASE:
		{
			writeBits(OPCODE_ERASE, INSTRUCTION_WIDTH, instruction, bitLength);

			if(!usingBlock)
				writeBits(extractBlockID(command.mainAddress), blockIDBits, instruction, bitLength);

			break;
		}

		case LOAD_AND_FLUSH:
		{
			writeBits(OPCODE_LOAD_FLUSH, INSTRUCTION_WIDTH, instruction, bitLength);

			if(!usingBlock)
				writeBits(extractBlockID(command.mainAddress), blockIDBits, instruction, bitLength);
			break;
		}

		case COMMIT:
		{
			writeBits(OPCODE_COMMIT, INSTRUCTION_WIDTH, instruction, bitLength);

			if(!usingBlock)
				writeBits(extractBlockID(command.mainAddress), blockIDBits, instruction, bitLength);
			break;
		}

		case FLUSH_AND_PARTIAL_COMMIT:
		{
			writeBits(OPCODE_FLUSH_COMMIT, INSTRUCTION_WIDTH, instruction, bitLength);

			if(!usingBlock)
				writeBits(extractBlockID(command.mainAddress), blockIDBits, instruction, bitLength);

			writeBits(command.length - 1, BLOCK_SIZE_BIT, instruction, bitLength);
			break;
		}

		case COPY:
		{
			const bool isMainCache = command.mainAddress >= CACHE_ADDRESS;
			const bool isSecCache = command.secondaryAddress >= CACHE_ADDRESS;

			if(isMainCache)
			{
				if(isSecCache)
					writeBits(OPCODE_COPY_CC, INSTRUCTION_WIDTH, instruction, bitLength);

				else
					writeBits(OPCODE_COPY_CN, INSTRUCTION_WIDTH, instruction, bitLength);
			}
			else
			{
				if(isSecCache)
				{
					writeBits(OPCODE_COPY_NC, INSTRUCTION_WIDTH, instruction, bitLength);
				}
				else
				{
					writeBits(OPCODE_COPY_NN, INSTRUCTION_WIDTH, instruction, bitLength);
				}

				if(!usingBlock)
					writeBits(extractBlockID(command.mainAddress), blockIDBits, instruction, bitLength);
			}

			writeBits(command.mainAddress, BLOCK_SIZE_BIT, instruction, bitLength);
			writeBits(command.length - 1, BLOCK_SIZE_BIT, instruction, bitLength);

			//Write the second block BlockID if relevant
			//	We don't write the second BlockID if the first operand was from the cache (as we had no opportunity to use the block mentionned by USE_BLOCK)
			if(!isSecCache && (!usingBlock || (usingBlock && !isMainCache)))
				writeBits(extractBlockID(command.secondaryAddress), blockIDBits, instruction, bitLength);

			writeBits(command.secondaryAddress, BLOCK_SIZE_BIT, instruction, bitLength);
			break;
		}

		case CHAINED_COPY:
		{
			if(command.mainAddress < CACHE_ADDRESS)
			{
				writeBits(OPCODE_CHAINED_COPY_N, INSTRUCTION_WIDTH, instruction, bitLength);

				if(!usingBlock)
					writeBits(extractBlockID(command.mainAddress), blockIDBits, instruction, bitLength);
			}
			else
				writeBits(OPCODE_CHAINED_COPY_C, INSTRUCTION_WIDTH, instruction, bitLength);

			writeBits(command.mainAddress, BLOCK_SIZE_BIT, instruction, bitLength);
			writeBits(command.length - 1, BLOCK_SIZE_BIT, instruction, bitLength);

			break;
		}

		case CHAINED_COPY_SKIP:
		{
			writeBits(OPCODE_CHAINED_SKIP, INSTRUCTION_WIDTH, instruction, bitLength);
			writeBits(command.length - 1, MAX_SKIP_LENGTH_BITS, instruction, bitLength);
			break;
		}

		case USE_BLOCK:
		{
			writeBits(OPCODE_USE_BLOCK, INSTRUCTION_WIDTH, instruction, bitLength);
			writeBits(extractBlockID(command.mainAddress), blockIDBits, instruction, bitLength);
			usingBlock = true;
			break;
		}
		case RELEASE_BLOCK:
		{
			writeBits(OPCODE_RELEASE, INSTRUCTION_WIDTH, instruction, bitLength);
			usingBlock = false;
			break;
		}
		case REBASE:
		{
			blockBase.value = 0;
			blockIDBits = numberOfBitsNecessary(command.length);

			assert(numberOfBitsNecessary(blockIDBits - 1u) <= REBASE_LENGTH_BITS);

			writeBits(OPCODE_REBASE, INSTRUCTION_WIDTH, instruction, bitLength);
			writeBits(extractBlockID(command.mainAddress), BLOCK_ID_SPACE, instruction, bitLength);
			writeBits(blockIDBits - 1u, REBASE_LENGTH_BITS, instruction, bitLength);

			blockBase = command.mainAddress;
			break;
		}

		case END_OF_STREAM:
		{
			writeBits(OPCODE_END_OF_STREAM, INSTRUCTION_WIDTH, instruction, bitLength);
			break;
		}
	}
}

void Encoder::appendInstruction(const PublicCommand & command, uint8_t & currentByte, uint8_t & spaceLeftInByte, std::vector<uint8_t> &byteStream)
{
	uint8_t bitLength;
	std::vector<uint64_t> instruction;

	encodeInstruction(command, bitLength, instruction);

	//We append the qword one by one
	for(uint64_t qword : instruction)
	{
		//We get the relevant length
		uint8_t qwordLength = MIN(bitLength, 8 * sizeof(qword));
		bitLength -= qwordLength;

		//We convert the instruction to big endian (the stronger bits i.e. the opcode first)
		while(qwordLength > spaceLeftInByte)
		{
			//We fill currentByte

			//	bitLength - spaceLeftInByte gives us the shift we need to have `spaceLeftInByte` usefull bytes
			//	We mask to ignore stronger bits

			currentByte |= (qword >> (qwordLength - spaceLeftInByte)) & MASK_OF_WIDTH(spaceLeftInByte);
			qwordLength -= spaceLeftInByte;

			//Commit the byte
			byteStream.push_back(currentByte);

			//Reset the byte
			currentByte = 0;
			spaceLeftInByte = 8;
		}

		//We can then shove the bits left in the most significant bits of currentByte
		//Assuming spaceLeftInByte is 7 (strongest bit in use) and bitLength is 6 (on bit will be left untouched)
		//	This would me equivalent to (6 lowest bits of instructions) << 1

		currentByte |= (qword & MASK_OF_WIDTH(qwordLength)) << (spaceLeftInByte - qwordLength);
		spaceLeftInByte -= qwordLength;
	}
}

void Encoder::encode(const std::vector<PublicCommand> & commands, uint8_t* & _byteField, size_t & length)
{
	reset();

	std::vector<uint8_t> byteField;
	uint8_t currentByte = 0, spaceLeftInByte = 8;

	for(const auto & command : commands)
		appendInstruction(command, currentByte, spaceLeftInByte, byteField);

	//The stream must finish with at least INSTRUCT_WIDTH worth of 1 to be parsed as OPCODE_END_OF_STREAM
	if(spaceLeftInByte == 8 || spaceLeftInByte < INSTRUCTION_WIDTH)
	{
		appendInstruction(PublicCommand{
				.command = END_OF_STREAM,
				.mainAddress = 0,
				.secondaryAddress = 0,
				.length = 0
		}, currentByte, spaceLeftInByte, byteField);
	}

	//currentByte partially in use, we pad it with ones
	if(spaceLeftInByte != 8)
	{
		currentByte |= MASK_OF_WIDTH(spaceLeftInByte);
		byteField.push_back(currentByte);
	}

	length = byteField.size();
	_byteField = (uint8_t *) malloc(length);
	if(_byteField != nullptr)
	{
		size_t index = 0;
		for(const auto & byte : byteField)
			_byteField[index++] = byte;
	}
}

bool Encoder::_decodeInstruction(const uint8_t * byteStream, size_t & currentByteOffset, const size_t length, PublicCommand & command)
{
	DecodedCommand cCommand;
	DecoderContext decoderContext = {
			.usingBlock = usingBlock,
			.blockInUse = blockInUse.value,
			.blockIDBits = blockIDBits,
			.blockBase = blockBase.value,
			.blockIDBitsRef = BLOCK_ID_SPACE,
			.blockSizeBitsRef = BLOCK_SIZE_BIT
	};

	decodeInstruction(&decoderContext, byteStream, &currentByteOffset, length, &cCommand);

	blockBase.value = decoderContext.blockBase;
	blockIDBits = decoderContext.blockIDBits;
	blockInUse = decoderContext.blockInUse;
	usingBlock = decoderContext.usingBlock;

	command.mainAddress = cCommand.mainAddress;
	command.secondaryAddress = cCommand.secondaryAddress;
	command.length = cCommand.length;

	switch(cCommand.command)
	{
		case OPCODE_ERASE:
		{
			command.command = ERASE;
			break;
		}
		case OPCODE_LOAD_FLUSH:
		{
			command.command = LOAD_AND_FLUSH;
			break;
		}
		case OPCODE_COMMIT:
		{
			command.command = COMMIT;
			break;
		}
		case OPCODE_FLUSH_COMMIT:
		{
			command.command = FLUSH_AND_PARTIAL_COMMIT;
			break;
		}
		case OPCODE_USE_BLOCK:
		{
			command.command = USE_BLOCK;
			break;
		}
		case OPCODE_RELEASE:
		{
			command.command = RELEASE_BLOCK;
			break;
		}
		case OPCODE_REBASE:
		{
			command.command = REBASE;
			break;
		}
		case OPCODE_COPY_NN:
		case OPCODE_COPY_NC:
		case OPCODE_COPY_CN:
		case OPCODE_COPY_CC:
		{
			const bool isMainCache = (cCommand.command == OPCODE_COPY_CN || cCommand.command == OPCODE_COPY_CC);
			const bool isSecCache = (cCommand.command == OPCODE_COPY_NC || cCommand.command == OPCODE_COPY_CC);

			if(isMainCache)
				command.mainAddress |= CACHE_ADDRESS;

			if(isSecCache)
				command.secondaryAddress |= CACHE_ADDRESS;

			command.command = COPY;

			break;
		}

		case OPCODE_CHAINED_COPY_C:
		{
			command.command = CHAINED_COPY;
			command.mainAddress |= CACHE_ADDRESS;
			break;
		}
		case OPCODE_CHAINED_COPY_N:
		{
			command.command = CHAINED_COPY;
			break;
		}

		case OPCODE_CHAINED_SKIP:
		{
			command.command = CHAINED_COPY_SKIP;
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

void Encoder::decode(const uint8_t * byteField, size_t length, std::vector<PublicCommand> & commands)
{
	reset();

	PublicCommand command = {};
	size_t currentOffset = 0;
	while(_decodeInstruction(byteField, currentOffset, length, command))
	{
		commands.push_back(command);
	}
}

size_t Encoder::validate(const std::vector<PublicCommand> & commands)
{
	uint8_t * bytes = nullptr;
	size_t length;

	encode(commands, bytes, length);

	if(bytes == nullptr)
		return 0;

	std::vector<PublicCommand> newCommands;
	decode(bytes, length, newCommands);

	free(bytes);

	if(commands.size() != newCommands.size())
		return 0;

	for(size_t i = 0, size = commands.size(); i < size; ++i)
	{
		const PublicCommand & old = commands[i];
		const PublicCommand & newer = newCommands[i];

		if(old.command != newer.command || old.mainAddress != newer.mainAddress
				|| old.length != newer.length || old.secondaryAddress != newer.secondaryAddress)
		{
			return 0;
		}
	}

	return length;
}

void Encoder::reset()
{
	usingBlock = false;
	blockIDBits = BLOCK_ID_SPACE;
	blockBase = 0;
}