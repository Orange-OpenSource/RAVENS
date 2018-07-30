//
// Created by Emile-Hugo Spir on 4/10/18.
//

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
