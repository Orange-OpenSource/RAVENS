//
// Created by Emile-Hugo Spir on 4/30/18.
//

#ifndef HERMES_LZFX_LIGHT_H
#define HERMES_LZFX_LIGHT_H

#define LZFX_ECORRUPT   (-2)      /* Invalid data for decompression */
#define LZFX_EARGS      (-3)      /* Arguments invalid (NULL) */

typedef enum
{
	LZFX_OK = 0,
	LZFX_SUSPEND_LITTERAL,
	LZFX_SUSPEND_DECOMPRESS,
	LZFX_DONE
} LZFX_STATUS;

typedef struct
{
	//LZFX Specific code
	const uint8_t * input;
	uint32_t currentInputOffset;
	size_t inputLength;

	uint8_t * referenceOutput;
	uint8_t * output;
	uint16_t outputRealSize;


	//Restore of context
	LZFX_STATUS status;
	uint32_t lengthToRead;
	uint16_t backRef;

} Lzfx4KContext;

typedef struct
{
	Lzfx4KContext lzfx;

	uint16_t currentCacheOffset;
	uint16_t lengthLeft;

	bool isOutOfData;

} BSDiffContext;

int lzfx_decompress(Lzfx4KContext * context, uint16_t *outputLength);

#endif //HERMES_LZFX_LIGHT_H
