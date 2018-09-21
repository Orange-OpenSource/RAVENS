/*
 * Copyright (C) 2018 Orange
 *
 * This software is distributed under the terms and conditions of the 'BSD-3-Clause-Clear'
 * license which can be found in the file 'LICENSE.txt' in this package distribution
 * or at 'https://spdx.org/licenses/BSD-3-Clause-Clear.html'.
 */

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
