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

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include "lzfx_light.h"
#include "../core.h"

#if __GNUC__ >= 3
# define fx_expect_false(expr)  __builtin_expect((expr) != 0, 0)
# define fx_expect_true(expr)   __builtin_expect((expr) != 0, 1)
#else
# define fx_expect_false(expr)  (expr)
# define fx_expect_true(expr)   (expr)
#endif

/* These cannot be changed, as they are related to the compressed format. */
#define MAX_OFF_FORMAT_1 (1u << 11u)

HERMES_CRITICAL uint8_t * getOutputPointerWithBackOffset(Lzfx4KContext * context, uint16_t backOffset)
{
	uint16_t currentOffset = (uint16_t) (context->output - context->referenceOutput);
	backOffset &= context->outputRealSize - 1u;

	if(backOffset <= currentOffset)
		return context->output - backOffset;

	return context->referenceOutput + context->outputRealSize + currentOffset - backOffset;
}

HERMES_CRITICAL uint16_t availableRoomBeforeLoopback(Lzfx4KContext * context, const uint8_t * ptr)
{
	if(ptr < context->referenceOutput || ptr >= context->referenceOutput + context->outputRealSize)
		return 0;

	return (uint16_t) (context->outputRealSize - (ptr - context->referenceOutput));
}

HERMES_CRITICAL void resumeCurrentSegment(Lzfx4KContext * context, const uint16_t outputLength)
{
	if(context->status == LZFX_OK)
		return;

	//Get the frozen context
	uint32_t length = context->lengthToRead;
	uint16_t backRef = context->backRef;
	bool needReuse = context->status == LZFX_SUSPEND_DECOMPRESS;

	//Check whether we can process it all in a single run
	if(length > outputLength)
	{
		context->lengthToRead -= outputLength;
		if(needReuse)
			context->backRef = (uint16_t) ((uint16_t) (outputLength + context->backRef) & (context->outputRealSize - 1u));

		length = outputLength;
	}
	else
		context->status = LZFX_OK;

	//Is it a literal copy or a data reuse
	if(needReuse)
	{
		//We get the pointer to where we have to copy data from
		uint8_t *ref = getOutputPointerWithBackOffset(context, backRef);

		while(length)
		{
			//Check how far we are of the end of the buffer
			uint16_t spaceLeft = availableRoomBeforeLoopback(context, ref);

			//If we will finish the copy with the output space we have, we signal the end of the loop
			if(spaceLeft >= length)
			{
				spaceLeft = (uint16_t) length;
				length = 0;
			}
			else
				length -= spaceLeft;

			//Perform the copy
			while(spaceLeft--)
				*context->output++ = *ref++;

			//Reset the buffer
			ref = context->referenceOutput;
		}
	}
	else
	{
		while(length--)
			*context->output++ = context->input[context->currentInputOffset++];
	}
}

HERMES_CRITICAL int lzfx_decompress(Lzfx4KContext * context, uint16_t *outputLength)
{
	if (outputLength == NULL)
		return LZFX_EARGS;

	if(context->output == NULL || context->status == LZFX_DONE)
	{
		*outputLength = 0;
		return LZFX_EARGS;
	}

	if (context->input == NULL)
	{
		*outputLength = 0;

		if (context->inputLength != 0)
			return LZFX_EARGS;

		return LZFX_OK;
	}

	const uint8_t * outputEnd = context->output + *outputLength, * originalOutput = context->output;

	resumeCurrentSegment(context, *outputLength);

	const uint8_t *inputBuffer = context->input + context->currentInputOffset, *originalInput = inputBuffer;
	const uint8_t * inputEnd = context->input + context->inputLength;

	while (inputBuffer < inputEnd && context->status == LZFX_OK)
	{
		uint8_t ctrl = *inputBuffer++;

		/*  Format #1 [1LLLLLoo oooooooo]: backref of length L + 2
							 ^^ ^^^^^^^^
							 A     B
				   #2 [11111Loo oooooooo ooLLLLLL] backref of length D << 1 + C + 30 (if the backshift is low enough not to have caused the use of format 2) + 2
							'^^ ^^^^^^^^ ^^^^^^^^
							C A    B     E   D
			In both cases the location of the backref is computed from the
			remaining part of the data as follows:

				location = outputBuffer - A - B << 2 - E << 10 - 1
		*/
		if (ctrl & 0x80u)
		{
			if (fx_expect_false(inputBuffer >= inputEnd))
				return LZFX_ECORRUPT;

			unsigned int length;
			uint16_t opOffset = (uint16_t) (ctrl & 0b11u) + (*inputBuffer++ << 2u);

			const bool isFormat2 = ctrl >> 3u == 0b11111; //i.e. format #2

			if (isFormat2)
			{
				if (fx_expect_false(inputBuffer >= inputEnd))
					return LZFX_ECORRUPT;

				const uint8_t thirdByte = *inputBuffer++;

				opOffset += (thirdByte >> 6u) << 10u;
				length = ((unsigned int) thirdByte & 0b111111u) << 1u;
				length |= (ctrl >> 2u) & 0x1u;

				if(opOffset < MAX_OFF_FORMAT_1)
					length += 0b11110;
			}
			else
				length = (ctrl >> 2u) & 0b11111u;

			uint8_t *ref = getOutputPointerWithBackOffset(context, ++opOffset);
			length += 2;

			if (fx_expect_false(context->output + length > outputEnd))
			{
				//Reduce the amount of data to compute on this round
				context->status = LZFX_SUSPEND_DECOMPRESS;
				context->lengthToRead = length - (uint32_t) (outputEnd - context->output);
				length -= context->lengthToRead;
				context->backRef = (uint16_t) (opOffset - length);
			}

			//If we will need to loop back at the beginning of the ring buffer
			while(length)
			{
				uint16_t spaceLeft = availableRoomBeforeLoopback(context, ref);

				if(spaceLeft >= length)
					spaceLeft = (uint16_t) length;

				length -= spaceLeft;
				while(spaceLeft--)
					*context->output++ = *ref++;

				ref = context->referenceOutput;
			}
		}

			/* Format 0LLLLLLL: a literal byte string follows, of length L+1 */
		else
		{
			ctrl++;

			if (fx_expect_false(context->output + ctrl > outputEnd))
			{
				//Only compute what we can in this round
				context->status = LZFX_SUSPEND_LITTERAL;
				context->lengthToRead = ctrl - (uint32_t) (outputEnd - context->output);
				ctrl -= context->lengthToRead;
			}

			if (fx_expect_false(inputBuffer + ctrl > inputEnd))
				return LZFX_ECORRUPT;

			do
			{
				*context->output++ = *inputBuffer++;
			} while (--ctrl);
		}

	}

	if(context->status == LZFX_OK)
		context->status = LZFX_DONE;

	*outputLength = (uint16_t) (context->output - originalOutput);
	context->currentInputOffset += (inputBuffer - originalInput);
	return LZFX_OK;
}