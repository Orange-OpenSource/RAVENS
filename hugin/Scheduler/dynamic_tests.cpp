//
// Created by Emile-Hugo Spir on 3/30/18.
//


#include <cstdint>
#include <cstdio>
#include <iostream>
#include <vector>

#include "public_command.h"
#include "bsdiff/bsdiff.h"
#include "Encoding/encoder.h"
#include "validation.h"

using namespace std;

bool runDynamicTest(const uint8_t * original, size_t originalLength, const uint8_t * newer, size_t newLength)
{
	bool output = true;
	SchedulerPatch patch;

	//We set the address space to the largest binary
	_realFullAddressSpace = numberOfBitsNecessary(originalLength > newLength ? originalLength : newLength);

	//Generate the patch
	if(generatePatch(original, originalLength, newer, newLength, patch, false))
	{
		//If the files are identical, we're done
		if(patch.bsdiff.empty())
		{
			goto cleanup;
		}

		//Perform semantic validations
		if(!validateSchedulerPatch(original, originalLength, newer, newLength, patch))
		{
			output = false;
			goto cleanup;
		}

		//We can then generate the real file
		FILE *file = tmpfile();
		if(file == nullptr)
		{
			cerr << "Couldn't open output file!" << endl;
			output = false;
			goto cleanup;
		}

		size_t length;
		uint8_t * encodedCommands = nullptr;
		Encoder encoder;
		encoder.encode(patch.commands, encodedCommands, length);
		free(encodedCommands);

		cout << "Encoded command payload would take " << length << " bytes." << endl;

		if(!writeBSDiff(patch, file))
		{
			cerr << "BSDiff write failed!" << endl;
			fclose(file);
			output = false;
			goto cleanup;
		}


		auto fileSize = ftello(file);
		fclose(file);

		if(fileSize < 0)
		{
			cerr << "Couldn't determine filesize!" << endl;
			output = false;
			goto cleanup;
		}

		cout << "The full BSDiff patch weight a total of " << fileSize << " bytes." << endl;
	}
	else
	{
		cerr << "Couldn't validate the BSDiff!" << endl;
		output = false;
		goto cleanup;
	}

cleanup:

	patch.clear(true);
	return output;
}

bool runDynamicTestWithFiles(const char * original, const char * newFile)
{
	size_t originalLength, newLength;

	uint8_t * originalData = readFile(original, &originalLength);
	uint8_t * newData = readFile(newFile, &newLength);

	bool output = runDynamicTest(originalData, originalLength, newData, newLength);

	free(originalData);
	free(newData);

	if(output)
	{
		cout << "Dynamic test successful!" << endl << endl;
	}
	else
	{
		cout << "Dynamic test failure!" << endl << endl;
	}

	return output;
}