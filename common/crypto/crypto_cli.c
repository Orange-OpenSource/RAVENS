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

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "crypto_cli.h"
#include "crypto.h"

void printCryptoHelp()
{
	puts("Possible arguments:\n\
	 --generateKeys privateKeyFile publicKeyFile\n\
	 --testRoundtrip stringMessage [privateKeyFile publicKeyFile]\n\
	 --signFile fileToSign outputFile privateKeyFile\n\
	 --verifyFile fileToVerify publicKeyFile\n\
	 --signString hexEncodedstringToSign privateKeyFile\n\
	 --verifyString stringToVerify hexEncodedSignature publicKeyFile\n\
	 --verifyStringHex hexEncodedStringToVerify hexEncodedSignature publicKeyFile\n");
}

bool processCrypto(int argc, char const *argv[])
{
	if(argc <= 1)
		return false;

	if(!strcmp(argv[1], "--generateKeys") && argc == 4)
	{
		generateKeys(argv[2], argv[3]);
	}
	else if(!strcmp(argv[1], "--testRoundtrip") && argc >= 3)
	{
		if(argc < 5)
		{
			testSignature((const unsigned char *) argv[2], false, NULL, NULL);
			testSignature((const unsigned char *) argv[2], true, NULL, NULL);
		}
		else
		{
			testSignature((const unsigned char *) argv[2], false, argv[3], argv[4]);
			testSignature((const unsigned char *) argv[2], true, argv[3], argv[4]);
		}
	}
	else if(!strcmp(argv[1], "--signFile") && argc == 5)
	{
		signFile(argv[2], argv[3], argv[4]);
	}
	else if(!strcmp(argv[1], "--verifyFile") && argc == 4)
	{
		verifyFile(argv[2], argv[3]);
	}
	else if(!strcmp(argv[1], "--signString") && argc == 4)
	{
		unsigned char signature [SIGNATURE_LENGTH];
		if(signString(argv[2], argv[3], signature))
		{
			char signatureHex[SIGNATURE_LENGTH * 2 + 1];
			hydro_bin2hex(signatureHex, sizeof(signatureHex), signature, sizeof(signature));
			fprintf(stdout, "Signature: %s", signatureHex);
		}
	}
	else if(!strcmp(argv[1], "--verifyString") && argc == 5)
	{
		verifyString((const unsigned char *) argv[2], strlen(argv[2]), argv[3], false, argv[4]);
	}
	else if(!strcmp(argv[1], "--verifyStringHex") && argc == 5)
	{
		verifyString((const unsigned char *) argv[2], strlen(argv[2]), argv[3], true, argv[4]);
	}
	else
		return false;

	return true;
}