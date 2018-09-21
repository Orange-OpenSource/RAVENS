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

#include <cstdlib>
#include <cstdint>
#include <vector>
#include <string>
#include "encoder.h"

uint8_t numberOfBitsNecessary(size_t x)
{
	uint8_t output = 0;

	while(x)
	{
		x >>= 1;
		output += 1;
	}

	return output;
}
