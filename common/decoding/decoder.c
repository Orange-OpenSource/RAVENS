//
// Created by Emile-Hugo Spir on 4/25/18.
//

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#ifdef TARGET_LIKE_MBED
	#include "core.h"
#else
	#define HERMES_CRITICAL
#endif

#include "decoder.h"

HERMES_CRITICAL uint64_t readBits(const uint8_t * bitField, size_t * currentOffset, size_t length, uint8_t lengthToRead)
{
	uint64_t output = 0;

	while (lengthToRead)
	{
		assert(*currentOffset >> 8u < length);

		const uint8_t lengthLeftInByte = (uint8_t) (8 - (*currentOffset & 7u));
		const uint8_t currentByte = (uint8_t) (bitField[*currentOffset >> 3u] & MASK_OF_WIDTH(lengthLeftInByte));

		//If we're not going to consume the full byte
		if (lengthToRead < lengthLeftInByte)
		{
			//Make room for the new bits
			output <<= lengthToRead;

			//Write the bits after shifting right to remove the bits we're not interesting in
			output |= currentByte >> (lengthLeftInByte - lengthToRead);

			//Nothing left to read!
			*currentOffset += lengthToRead;
			lengthToRead = 0;
		}
		else
		{
			//We shift output by as many bits as we have left
			output <<= lengthLeftInByte;
			output |= currentByte;

			lengthToRead -= lengthLeftInByte;
			*currentOffset += lengthLeftInByte;
		}
	}

	return output;
}

HERMES_CRITICAL size_t readBlockID(DecoderContext * context, const uint8_t * byteStream, size_t * currentByteOffset, size_t length)
{
	const size_t baseBlockID = readBits(byteStream, currentByteOffset, length, context->blockIDBits) + context->blockBase;

	return (baseBlockID & MASK_OF_WIDTH(context->blockIDBits)) << context->blockSizeBitsRef;
}

HERMES_CRITICAL bool decodeInstruction(DecoderContext * context, const uint8_t * byteStream, size_t * currentByteOffset, const size_t length, DecodedCommand * command)
{
	command->mainAddress = command->secondaryAddress = command->length = 0;

	uint64_t instruction = readBits(byteStream, currentByteOffset, length, INSTRUCTION_WIDTH);
	command->command = (OPCODE) instruction;

	switch (instruction)
	{
		//Those three cases are identical except when writing the command
		case OPCODE_ERASE:
		case OPCODE_LOAD_FLUSH:
		case OPCODE_COMMIT:
		{
			if (context->usingBlock)
				command->mainAddress = context->blockInUse;
			else
				command->mainAddress = readBlockID(context, byteStream, currentByteOffset, length);

			break;
		}

		case OPCODE_FLUSH_COMMIT:
		{
			if (context->usingBlock)
				command->mainAddress = context->blockInUse;
			else
				command->mainAddress = readBlockID(context, byteStream, currentByteOffset, length);

			command->length = readBits(byteStream, currentByteOffset, length, context->blockSizeBitsRef) + 1;
			break;
		}

		case OPCODE_USE_BLOCK:
		{
			context->blockInUse = readBlockID(context, byteStream, currentByteOffset, length);
			context->usingBlock = true;

			command->mainAddress = context->blockInUse;
			break;
		}

		case OPCODE_RELEASE:
		case OPCODE_END_OF_STREAM:
		{
			context->usingBlock = false;
			break;
		}

		case OPCODE_COPY_NN:
		case OPCODE_COPY_NC:
		case OPCODE_COPY_CN:
		case OPCODE_COPY_CC:
		{
			const bool isMainCache = (instruction == OPCODE_COPY_CN || instruction == OPCODE_COPY_CC);
			const bool isSecCache = (instruction == OPCODE_COPY_NC || instruction == OPCODE_COPY_CC);

			if (isMainCache)
			{
				command->mainAddress = 0;
			}
			else
			{
				if (context->usingBlock)
					command->mainAddress = context->blockInUse;
				else
					command->mainAddress = readBlockID(context, byteStream, currentByteOffset, length);
			}

			command->mainAddress |= readBits(byteStream, currentByteOffset, length, context->blockSizeBitsRef);
			command->length = readBits(byteStream, currentByteOffset, length, context->blockSizeBitsRef) + 1;

			if (isSecCache)
			{
				command->secondaryAddress = 0;
			}
			else
			{
				if (context->usingBlock && isMainCache)
					command->secondaryAddress = context->blockInUse;
				else
					command->secondaryAddress = readBlockID(context, byteStream, currentByteOffset, length);
			}

			command->secondaryAddress |= readBits(byteStream, currentByteOffset, length, context->blockSizeBitsRef);
			break;
		}

		case OPCODE_CHAINED_COPY_N:
		case OPCODE_CHAINED_COPY_C:
		{
			if (instruction == OPCODE_CHAINED_COPY_C)
				command->mainAddress = 0;
			else
			{
				if (context->usingBlock)
					command->mainAddress = context->blockInUse;
				else
					command->mainAddress = readBlockID(context, byteStream, currentByteOffset, length);
			}

			command->mainAddress |= readBits(byteStream, currentByteOffset, length, context->blockSizeBitsRef);
			command->length = readBits(byteStream, currentByteOffset, length, context->blockSizeBitsRef) + 1;
			break;
		}

		case OPCODE_CHAINED_SKIP:
		{
			command->length = readBits(byteStream, currentByteOffset, length, MAX_SKIP_LENGTH_BITS) + 1;
			break;
		}

		case OPCODE_REBASE:
		{
			context->blockIDBits = context->blockIDBitsRef;
			context->blockBase = 0;

			context->blockBase = readBlockID(context, byteStream, currentByteOffset, length);
			context->blockIDBits = (uint8_t) (readBits(byteStream, currentByteOffset, length, REBASE_LENGTH_BITS) + 1u);

			command->mainAddress = context->blockBase;
			command->length = (1u << context->blockIDBits) - 1u;
			break;
		}

		default:
		{
			//Unknown instruction
			command->command = OPCODE_ILLEGAL;
			return false;
		}
	}

	return true;
}