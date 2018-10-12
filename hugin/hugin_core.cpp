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

#include <iostream>
#include <ostream>
#include <cstring>
#include <vector>
#include <crypto/crypto.h>
#include "CLI/scheduler_cli.h"

extern "C"
{
	#include <crypto_cli.h>
}

using namespace std;

void printCryptoHelp();
bool processCrypto(int argc, char const *argv[]);

void performStaticTests();
bool runDynamicTestWithFiles(const char * original, const char * newFile);
void testCrypto();

int main(int argc, char *argv[])
{
	if(argc > 1)
	{
		if(!strcmp(argv[1], "crypto"))
		{
			if(!processCrypto(argc - 1, (const char **) &argv[1]))
				printCryptoHelp();

			return 0;
		}

		else if(!strcmp(argv[1], "diff"))
		{
			if(processScheduler(argc - 1, &argv[1]))
				cout << "Successful generation!" << endl;

			return 0;
		}

		else if(!strcmp(argv[1], "authenticate"))
		{
			if(!processAuthentication(argc - 1, &argv[1]))
				printAuthenticationHelp();

			return 0;
		}
		else if(!strcmp(argv[1], "test"))
		{
			cout << "Validating the code generation..." << endl << "	";
			performStaticTests();
			runDynamicTestWithFiles("/bin/ls", "/bin/cat");
			runDynamicTestWithFiles("test1_v1.bin", "test1_v2.bin");
			runDynamicTestWithFiles("test2_v1.bin", "test2_v2.bin");

			cout << endl << "Validation cryptographic primitives" << endl;
			testCrypto();
			return 0;
		}
	}

	cerr << "Expected syntax: " << argv[0] << " [crypto | diff | authenticate | test]" << endl;
	return -1;
}
