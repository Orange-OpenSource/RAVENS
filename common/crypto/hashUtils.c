//
//	File: hashUtils.c
//
//	Purpose: Wrapper around our hash tools that enable the full signature verification pipeline
//
//	Author: Emile-Hugo Spir
//
//	Copyright: Orange
//

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "crypto.h"

bool signFile(const char *inputFile, const char *outputFile, const char *privKeyFile)
{
	if(inputFile == NULL || outputFile == NULL)
	{
		puts("Invalid arguments");
		return false;
	}

	uint8_t privKey[hydro_sign_SECRETKEYBYTES];
	loadKeyWithDefault(privKeyFile, true, privKey);

	uint8_t hashBuffer[HASH_LENGTH];
	if(!hashFile(inputFile, hashBuffer, 0))
	{
		puts("Couldn't hash the input file");
		return false;
	}

	uint8_t signature[SIGNATURE_LENGTH];
	if(!signBuffer(hashBuffer, HASH_LENGTH, signature, privKey))
	{
		puts("Signature failure");
		return false;
	}

	FILE * input = fopen(inputFile, "rb");
	FILE * output = fopen(outputFile, "wb");

	if(input == NULL || output == NULL)
	{
		puts("Couldn't open the files");
		fclose(input);
		fclose(output);
		return false;
	}

	fwrite(signature, SIGNATURE_LENGTH, 1, output);

	bool isEOF;
	do
	{
		uint8_t buffer[1024];
		size_t lengthRead = fread(buffer, 1, sizeof(buffer), input);

		isEOF = lengthRead != sizeof(buffer);

		fwrite(buffer, lengthRead, 1, output);

	} while(!isEOF);

	puts("Successful file generation.");

	fclose(input);
	fclose(output);

	return true;
}

bool signString(const char * inputString, const char * privKeyFile, uint8_t *signature)
{
	size_t length = strlen(inputString);

	if(length < 2 || length & 1u)
	{
		fprintf(stderr, "Invalid input length");
		return false;
	}

	uint8_t privKey[hydro_sign_SECRETKEYBYTES];
	loadKeyWithDefault(privKeyFile, true, privKey);

	//Decode the string
	uint8_t input[length >> 1];
	hydro_hex2bin(input, sizeof(input), inputString, length, NULL, NULL);

	if(!signBuffer(input, sizeof(input), signature, privKey))
	{
		fputs("Signature fail", stderr);
		return false;
	}

	return true;
}

bool verifyFile(const char *inputFile, const char * pubKeyFile)
{
	if(inputFile == NULL)
	{
		puts("Invalid arguments");
		return false;
	}

	uint8_t hashBuffer[HASH_LENGTH];
	if(!hashFile(inputFile, hashBuffer, SIGNATURE_LENGTH))
	{
		puts("Couldn't hash the file");
		return false;
	}

	uint8_t publicKey[hydro_sign_PUBLICKEYBYTES];
	loadKeyWithDefault(pubKeyFile, false, publicKey);

	FILE * file = fopen(inputFile, "rb");
	if(file == NULL)
	{
		puts("Couldn't open the file");
		return false;
	}

	uint8_t signatureBuffer[SIGNATURE_LENGTH];
	if(fread(signatureBuffer, 1, SIGNATURE_LENGTH, file) != SIGNATURE_LENGTH)
	{
		puts("File is incomplete.");
		fclose(file);
		return false;
	}

	fclose(file);

	if(!validateSignature(hashBuffer, sizeof(hashBuffer), signatureBuffer, publicKey))
	{
		fputs("Invalid signature!\n", stderr);
		return false;
	}

	puts("Success!");
	return true;
}

bool verifyString(const uint8_t *inputString, size_t stringLength, const char *signature, bool isHex, const char * pubKeyFile)
{
	if(inputString == NULL || stringLength < (isHex ? 2 : 1) || signature == NULL)
	{
		fputs("Invalid arguments\n", stderr);
		return false;
	}

	if(isHex)
	{
		uint8_t decodedString[stringLength >> 1u];
		int decode = hydro_hex2bin(decodedString, sizeof(decodedString), (const char *) inputString, stringLength, NULL, NULL);
		if(decode < 0 || (size_t) decode != stringLength)
		{
			fputs("Couldn't decode the hex string\n", stderr);
			return false;
		}

		return verifyString(decodedString, sizeof(decodedString), signature, false, pubKeyFile);
	}

	if(strlen(signature) != SIGNATURE_LENGTH * 2)
	{
		fputs("Invalid signature format\n", stderr);
		return false;
	}

	uint8_t decodedSignature[SIGNATURE_LENGTH];
	if(hydro_hex2bin(decodedSignature, SIGNATURE_LENGTH, signature, 2 * SIGNATURE_LENGTH, NULL, NULL) != SIGNATURE_LENGTH)
	{
		fputs("Couldn't decode the signature\n", stderr);
		return false;
	}

	uint8_t publicKey[hydro_sign_PUBLICKEYBYTES];
	loadKeyWithDefault(pubKeyFile, false, publicKey);

	if(!validateSignature(inputString, stringLength, decodedSignature, publicKey))
	{
		puts("Invalid signature!");
		return false;
	}

	puts("Success!");
	return true;
}