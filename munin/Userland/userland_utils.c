//
// Created by Emile-Hugo Spir on 5/2/18.
//

#include <stdint.h>
#include <stddef.h>
#include <malloc.h>
#include "userland.h"
#include "../io_management.h"

#ifndef TARGET_LIKE_MBED

void itoa(uint32_t value, char * string, int base)
{
	char * baseString = string;

	//Write the digits to the buffer from the least significant to the most
	//  This is the incorrect order but we will swap later
	do {
		const uint32_t oldValue = value;
		value /= base;

		*string = (char) ('0' + (oldValue - value * base));
		string++;

	} while(value);

	//We put the final \0, then go back one step on the last digit for the swap
	*string-- = '\0';

	//We now swap the digits
	while(baseString < string) {
		char tmp = *string;
		*string-- = *baseString;
		*baseString++ = tmp;
	}
}
#endif

static const char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
									  'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
									  'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
									  'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
									  'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
									  'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
									  'w', 'x', 'y', 'z', '0', '1', '2', '3',
									  '4', '5', '6', '7', '8', '9', '+', '-'};
static const int mod_table[] = {0, 2, 1};

uint32_t base64_encode(const unsigned char *data, uint32_t input_length, char * output)
{
	uint32_t local_output_length = 4 * ((input_length + 2) / 3);

	for (size_t i = 0; i < input_length;)
	{
		uint32_t byteA = i < input_length ? data[i++] : 0;
		uint32_t byteB = i < input_length ? data[i++] : 0;
		uint32_t byteC = i < input_length ? data[i++] : 0;

		uint32_t triple = (byteA << 0x10u) + (byteB << 0x08u) + byteC;

		*output++ = encoding_table[(triple >> 3u * 6) & 0x3Fu];
		*output++ = encoding_table[(triple >> 2u * 6) & 0x3Fu];
		*output++ = encoding_table[(triple >> 1u * 6) & 0x3Fu];
		*output++ = encoding_table[(triple >> 0u * 6) & 0x3Fu];
	}

	for (uint8_t i = 0; i < mod_table[input_length % 3]; i++)
		*output++ = '=';

	*output = 0;

	return local_output_length;
}

void eraseNecessarySpace(const void * basePointer, uint32_t length)
{
	while(length > BLOCK_SIZE)
	{
		erasePage((size_t) basePointer);

		length -= BLOCK_SIZE;
		basePointer += BLOCK_SIZE;
	}

	erasePage((size_t) basePointer);
}