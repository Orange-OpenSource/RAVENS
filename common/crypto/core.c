//
//	File: core.c
//
//	Purpose: The entrypoint for the testing functions
//
//	Author: Emile-Hugo Spir
//
//	Copyright: Orange
//

#include <stdio.h>
#include <stdbool.h>
#include "crypto_cli.h"

void printHelp()
{
	puts("Need arguments!\n\
	Available commands are:\n");
	printCryptoHelp();
}

int main(int argc, char const *argv[])
{
	if(!processCrypto(argc, argv))
		printHelp();

	return 0;
}
