/*
 * Copyright (C) 2018 Orange
 *
 * This software is distributed under the terms and conditions of the 'BSD-3-Clause-Clear'
 * license which can be found in the file 'LICENSE.txt' in this package distribution
 * or at 'https://spdx.org/licenses/BSD-3-Clause-Clear.html'.
 */

/**
 * Purpose: Main source file for our hashing tools
 * @author Emile-Hugo Spir
 */

#include "sha256.h"
#include <stdbool.h>
#include <stdio.h>
#include "crypto.h"

#ifdef TARGET_LIKE_MBED
#include "../core.h"
#else
#define HERMES_CRITICAL
#endif

void hashBlock(const uint8_t * data, const size_t length, const uint16_t counter, bool reuseHash, uint8_t * hashBuffer)
{
	//	H(N+1) = H(N) . data . counter

	mbedtls_sha256_context ctx;

    mbedtls_sha256_init( &ctx );
	mbedtls_sha256_starts_ret( &ctx, 0 );

    if(!reuseHash)
		mbedtls_sha256_update_ret( &ctx, hashBuffer, HASH_LENGTH );

	mbedtls_sha256_update_ret( &ctx, data, length );
	mbedtls_sha256_update_ret( &ctx, (const uint8_t *) &counter, sizeof(counter) );

	mbedtls_sha256_finish_ret( &ctx, hashBuffer );
    mbedtls_sha256_free( &ctx );
}

HERMES_CRITICAL void hashMemory(const uint8_t * data, const size_t length, uint8_t * hashBuffer)
{
	mbedtls_sha256_ret(data, length, hashBuffer, 0);
}


bool hashFile(const char * filename, uint8_t * hashBuffer, size_t skip)
{
	FILE * file = fopen(filename, "rb");
	if(file == NULL)
		return false;

	if(skip != 0)
		fseek(file, skip, SEEK_SET);

	mbedtls_sha256_context ctx;
	mbedtls_sha256_init( &ctx );
	mbedtls_sha256_starts_ret( &ctx, 0 );

	bool isEOF;

	do
	{
		uint8_t buffer[1024];
		size_t lengthRead = fread(buffer, 1, 1024, file);
		isEOF = lengthRead != 1024;

		mbedtls_sha256_update_ret( &ctx, buffer, lengthRead);

	} while(!isEOF);

	mbedtls_sha256_finish_ret( &ctx, hashBuffer );
	mbedtls_sha256_free( &ctx );

	fclose(file);
	return true;
}
