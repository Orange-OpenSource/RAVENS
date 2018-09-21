/*
 * Copyright (C) 2018 Orange
 *
 * This software is distributed under the terms and conditions of the 'BSD-3-Clause-Clear'
 * license which can be found in the file 'LICENSE.txt' in this package distribution
 * or at 'https://spdx.org/licenses/BSD-3-Clause-Clear.html'.
 */

/**
 * Purpose: Some tools to manipulate and test libHydrogen based signatures
 * @author Emile-Hugo Spir
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "crypto.h"

//Should always return unless compiler got funny
bool clearMemory(uint8_t * memory, size_t length)
{
	if(length == 0)
		return true;

#ifdef __STDC_LIB_EXT1__
	memset_s(memory, 0, length);
#else
	memset(memory, 0, length);
	for(size_t i = 0; i < length - 1; ++i)
	{
		if(memory[i] != memory[i + 1])
			return false;
	}
#endif

return true;
}

bool loadKey(const char * filename, bool isPrivate, uint8_t * output)
{
	if(filename == NULL || output == NULL)
	{
		fputs("File error\n", stderr);
		return false;
	}

	FILE * file = fopen(filename, "rb");
	if(file == NULL)
	{
		fputs("File error\n", stderr);
		return false;
	}

	const size_t length = isPrivate ? hydro_sign_SECRETKEYBYTES : hydro_sign_PUBLICKEYBYTES;
	char buffer[length << 1u];

	bool retValue = fread(buffer, 1, sizeof(buffer), file) == sizeof(buffer);

	fclose(file);

	if(!retValue)
	{
		fputs("Read error", stderr);
		return false;
	}

	int encode = hydro_hex2bin(output, length, buffer, sizeof(buffer), NULL, NULL);

	clearMemory((uint8_t *) buffer, sizeof(buffer));

	return encode >= 0 && (size_t) encode == length;
}

void loadKeyWithDefault(const char * filename, bool isPrivate, uint8_t * output)
{
	if(!loadKey(filename, isPrivate, output))
	{
		if(isPrivate)
		{
			fputs("Using default private key\n", stderr);
			const uint8_t defaultSecret[] = SECRET_KEY;
			memcpy(output, defaultSecret, sizeof(defaultSecret));
		}
		else
		{
			fputs("Using default private key\n", stderr);
			const uint8_t defaultSecret[] = PUBLIC_KEY;
			memcpy(output, defaultSecret, sizeof(defaultSecret));
		}
	}
}

bool writeKey(uint8_t * key, bool isPrivate, const char * path)
{
	FILE * file = fopen(path, "wb");
	if(file == NULL)
		return false;

	bool retValue;

	if(isPrivate)
	{
		char outputPriv[2 * hydro_sign_SECRETKEYBYTES + 1];
		hydro_bin2hex(outputPriv, sizeof(outputPriv), key, hydro_sign_SECRETKEYBYTES);
		retValue = fwrite(outputPriv, 2 *  hydro_sign_SECRETKEYBYTES, 1, file) == 1;
		fclose(file);
	}
	else
	{
		char outputPub[2 * hydro_sign_PUBLICKEYBYTES + 1];
		hydro_bin2hex(outputPub, sizeof(outputPub), key, hydro_sign_PUBLICKEYBYTES);
		retValue = fwrite(outputPub, 2 *  hydro_sign_PUBLICKEYBYTES, 1, file) == 1;
		fclose(file);
	}

	return retValue;
}

bool generateKeys(const char * privKeyFile, const char * pubKeyFile)
{
	hydro_sign_keypair kp;

	hydro_init();
	hydro_sign_keygen(&kp);

	if(!writeKey(kp.pk, false, pubKeyFile))
		return false;

	if(!writeKey(kp.sk, true, privKeyFile))
		return false;

	clearMemory(kp.pk, sizeof(kp.pk));
	clearMemory(kp.sk, sizeof(kp.sk));

	puts("\tSuccessful key generation");
	return true;
}

void generateKeyMemory(uint8_t * secretKey, uint8_t * publicKey)
{
	hydro_sign_keypair kp;

	hydro_init();
	hydro_sign_keygen(&kp);

	memcpy(secretKey, kp.sk, hydro_sign_SECRETKEYBYTES);
	memcpy(publicKey, kp.pk, hydro_sign_PUBLICKEYBYTES);

	clearMemory((uint8_t *) &kp, sizeof(kp));
}

void printSignature(const uint8_t sig[hydro_sign_BYTES])
{
	char signature[2 * hydro_sign_BYTES + 1];

	if(hydro_bin2hex(signature, sizeof(signature), sig, hydro_sign_BYTES))
	{
		printf("Signature: %s\n", signature);
	}
	else
	{
		printf("Couldn't convert the signature\n");
	}
}

void testSignature(const uint8_t * message, bool wantFail, const char * privKeyFile, const char * pubKeyFile)
{
	const size_t messageLength = strlen((const char *) message);
	char hexMessage[messageLength * 2 + 1];

	hydro_bin2hex(hexMessage, sizeof(hexMessage), message, messageLength);

	//Generate the signature
	uint8_t sig[hydro_sign_BYTES];

	//hex encode the string
	if(!signString(hexMessage, privKeyFile, sig))
	{
		fputs("Signature failure\n", stderr);
		return;
	}

	if(wantFail)
	{
		uint8_t publicKey[hydro_sign_PUBLICKEYBYTES];
		loadKeyWithDefault(pubKeyFile, false, publicKey);

		puts("Checking incorrect signatures");

		//We flip every bit one by one and warn if the signature somehow validate
		for(size_t i = 0; i < hydro_sign_BYTES; ++i)
		{
			for(uint8_t j = 0; j < 8; ++j)
			{
				//Flip a bit
				sig[i] ^= 1u << j;

				if(validateSignature(message, messageLength, sig, publicKey))
				{
					fputs("Validated an incorrect signature!\n", stderr);
					return;
				}

				//Restore the bit
				sig[i] ^= 1u << j;
			}
		}

		puts("Signature test successful, detect bit flips.");
	}
	else
	{
		printf("Signing message: %s\n", (const char *) message);
		printSignature(sig);

		char signatureHex[SIGNATURE_LENGTH * 2 + 1];
		hydro_bin2hex(signatureHex, sizeof(signatureHex), sig, sizeof(sig));

		if(verifyString(message, messageLength, signatureHex, false, pubKeyFile))
		{
			printf("Success!\n");
		}
		else
		{
			fputs("FAIL!\n", stderr);
		}
	}
}

void testCrypto()
{
	char namePub[L_tmpnam], namePriv[L_tmpnam];
	if (!tmpnam(namePub) || !tmpnam(namePriv))
	{
		puts("\tIO error");
		return;
	}

	if(!generateKeys(namePriv, namePub))
		return;

	testSignature((const uint8_t*) "UGluayBmbHVmZnkgdW5pY29ybiBkYW5jaW5nIG9uIHJhaW5ib3dzDQpQaW5rIGZsdWZmeSB1bmljb3JuIGRhbmNpbmcgb24gcmFpbmJvd3MNClBpbmsgZmx1ZmZ5IHVuaWNvcm4gZGFuY2luZyBvbiByYWluYm93cw==", true, namePriv, namePub);
}

