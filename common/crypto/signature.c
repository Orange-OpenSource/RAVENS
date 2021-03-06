/*
 * Copyright (C) 2018 Orange
 *
 * This software is distributed under the terms and conditions of the 'BSD-3-Clause-Clear'
 * license which can be found in the file 'LICENSE.txt' in this package distribution
 * or at 'https://spdx.org/licenses/BSD-3-Clause-Clear.html'.
 */

/**
 * Purpose: Main source file for our signature tools
 * @author Emile-Hugo Spir
 */

#include "hydrogen.h"
#include "crypto_utils.h"

#ifdef TARGET_LIKE_MBED
#include "../core.h"
#else
#define RAVENS_CRITICAL
#endif


RAVENS_CRITICAL bool validateSignature(const uint8_t * message, const size_t messageLength, const uint8_t signature[hydro_sign_BYTES], const uint8_t publicKey[hydro_sign_PUBLICKEYBYTES])
{
	return hydro_sign_verify(signature, message, messageLength, CONTEXT, publicKey) == 0;
}

bool signBuffer(const uint8_t * message, const size_t messageLength, uint8_t signature[hydro_sign_BYTES], const uint8_t secretKey[hydro_sign_SECRETKEYBYTES])
{
	return hydro_sign_create(signature, message, messageLength, CONTEXT, secretKey) == 0;
}
