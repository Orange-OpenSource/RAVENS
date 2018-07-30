//
//	File: signature.c
//
//	Purpose: Main source file for our signature tools
//
//	Author: Emile-Hugo Spir
//
//	Copyright: Orange
//

#include "hydrogen.h"
#include "crypto.h"

#ifdef TARGET_LIKE_MBED
#include "../core.h"
#else
#define HERMES_CRITICAL
#endif


HERMES_CRITICAL bool validateSignature(const uint8_t * message, const size_t messageLength, const uint8_t signature[hydro_sign_BYTES], const uint8_t publicKey[hydro_sign_PUBLICKEYBYTES])
{
	return hydro_sign_verify(signature, message, messageLength, CONTEXT, publicKey) == 0;
}

bool signBuffer(const uint8_t * message, const size_t messageLength, uint8_t signature[hydro_sign_BYTES], const uint8_t secretKey[hydro_sign_SECRETKEYBYTES])
{
	return hydro_sign_create(signature, message, messageLength, CONTEXT, secretKey) == 0;
}