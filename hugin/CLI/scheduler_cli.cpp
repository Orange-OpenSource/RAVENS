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

#include <ostream>
#include <iostream>
#include <cstring>
#include <vector>
#include "../Scheduler/public_command.h"
#include "../Scheduler/bsdiff/bsdiff.h"
#include "../Scheduler/validation.h"
#include "scheduler_cli.h"

using namespace std;

void printSchedulerHelp()
{
	cout << endl << "The diff option lets you generate patch files between two binary images." << endl;
	cout << "If the --batchMode flag is used as the first argument, multiple patches will be generated in a new directory." << endl << endl;

	cout << "Mandatory arguments, if --batchMode:" << endl <<
		 "	[--config | -c] batchConfig" << endl <<
		 "	[--output | -o] outputDirectory" << endl << endl;

	cout << "Mandatory arguments, if not --batchMode:" << endl <<
		 "	[--original | -v1] oldFirmwareFile" << endl <<
		 "	[--new | -v2] newFirmwareFile" << endl <<
		 "	[--output | -o] outputFile" << endl << endl;

	cout << "Optional arguments:" << endl <<
"	--verbose		- Print additional information on the patch generation" << endl <<
"	--dryRun		- Perform the diff but doesn't actually write down the update. Useful for testing. Not valid in batchMode" << endl <<
"	--flashSize value	- Determine the address space. Value should be the power of two to be used." << endl <<
"				(e.g. 20 means that the flash is 2^20 bytes = 1MiB)" << endl <<
"				Default value is 20 (i.e. 1MiB)" << endl <<
"	--pageSize value	- Size of the flash pages to be by the scheduler. Value should be the power of two to be used." << endl <<
"				(e.g. 12 means that the flash is 2^12 bytes = 4KiB" << endl <<
"				Default value is 12 (i.e. 4096 bytes)" << endl <<
"	--diffAndSign" << endl << endl;
}

bool runSchedulerWithFiles(const char * oldFile, const char * newFile, const char * output, vector<VerificationRange> & preUpdateHashes, bool printLog, bool dryRun)
{
	if(oldFile == nullptr || newFile == nullptr || (output == nullptr && !dryRun))
	{
		printSchedulerHelp();
		return false;
	}

	SchedulerPatch patch{};

	size_t oldFileSize, newFileSize;

	uint8_t * oldFileContent = readFile(oldFile, &oldFileSize);
	if(oldFileContent == nullptr)
	{
		cerr << "Couldn't read the old firmware file" << endl;
		return false;
	}

	uint8_t * newFileContent = readFile(newFile, &newFileSize);
	if(newFileContent == nullptr)
	{
		cerr << "Couldn't read the new firmware file" << endl;
		return false;
	}

	bool retValue = true;

	//Generate the patch
	if(!generatePatch(oldFileContent, oldFileSize, newFileContent, newFileSize, patch, printLog))
	{
		cerr << "Couldn't diff the two firmware images. Please open a bug report!" << endl;
		retValue = false;
		goto cleanup;
	}

	//If the files are identical, we're done
	if(patch.bsdiff.empty())
	{
		goto cleanup;
	}

	//Perform semantic validations
	if(!validateSchedulerPatch(oldFileContent, oldFileSize, newFileContent, newFileSize, patch))
	{
		cerr << "Couldn't validate the diff between the two images. Please open a bug report!" << endl;
		retValue = false;
		goto cleanup;
	}

	//Restrict outputFile's scope
	if(!dryRun)
	{
		FILE * outputFile = fopen(output, "wb");
		retValue = outputFile != nullptr && writeBSDiff(patch, outputFile);
		if(outputFile != nullptr)
			fclose(outputFile);
	}

	preUpdateHashes = patch.oldRanges;
	patch.clear(true);

cleanup:

	free(newFileContent);
	free(oldFileContent);

	return retValue;
}

bool writeVerifRangeToFile(const vector<VerificationRange> & preUpdateHashes, const string &outputFile)
{
	if(preUpdateHashes.empty())
		return true;

	FILE * file = fopen(outputFile.c_str(), "w+");
	if(file == nullptr)
		return false;

	for(const auto & range : preUpdateHashes)
		fprintf(file, "%d, %d, %s\n", range.start, range.length, range.expectedHash.c_str());

	fclose(file);
	return true;
}

bool processScheduler(int argc, char *argv[])
{
	int index = 1;
	char * output = nullptr;

	if(argc > 1 && !strcmp(argv[1], "--batchMode"))
	{
		const char * config = nullptr;
		while(++index < argc)
		{
			if((!strcmp(argv[index], "--config") || !strcmp(argv[index], "-c")) && index + 1 < argc)
			{
				config = argv[index + 1];
				index += 1;
			}
			else if((!strcmp(argv[index], "--output") || !strcmp(argv[index], "-o")) && index + 1 < argc)
			{
				output = argv[index + 1];
				index += 1;
			}
			else
			{
				cerr << "Invalid argument: " << argv[index] << endl;
			}
		}

		if(config == nullptr || output == nullptr)
		{
			cerr << "Missing arguments" << endl;
			printSchedulerHelp();
			return false;
		}

		return processSchedulerBatch(config, output);
	}
	else
	{
		const char * oldFile = nullptr, * newFile = nullptr;
		bool wantLog = false, dryRun = false;
		while(index < argc)
		{
			if((!strcmp(argv[index], "--original") || !strcmp(argv[index], "-v1")) && index + 1 < argc)
			{
				oldFile = argv[index + 1];
				index += 2;
			}
			else if((!strcmp(argv[index], "--new") || !strcmp(argv[index], "-v2")) && index + 1 < argc)
			{
				newFile = argv[index + 1];
				index += 2;
			}
			else if((!strcmp(argv[index], "--output") || !strcmp(argv[index], "-o")) && index + 1 < argc)
			{
				output = argv[index + 1];
				index += 2;
			}
			else if(!strcmp(argv[index], "--dryRun"))
			{
				dryRun = true;
				index += 1;
			}
			else if(!strcmp(argv[index], "--verbose"))
			{
				wantLog = true;
				index += 1;
			}
			else if(!strcmp(argv[index], "--diffAndSign"))
			{
				cout << "This option isn't implemented and SHOULD NOT BE! It would be a SECURITY risk." << endl << endl;
				cout << "Signing a diff manifest require access to the device's (family) private key." << endl;
				cout << "This private key is the main defense protecting devices from forged, hacked updates." << endl;
				cout << "Therefore, the key should be held in a HSM, or a similarly secure infrastructure." << endl;
				cout << "At the bare minimum, the key should be held in a physically different, isolated server." << endl;
				cout << "Performing both the diff and the signature in a single command would strongly imply such isolation isn't achieved." << endl;
				cout << "The theft of this key would leave all devices using it at risk and changing it is a dangerous, unsupported update." << endl;
				return false;
			}
			else if(!strcmp(argv[index], "--flashSize") && index + 1 < argc)
			{
				_realFullAddressSpace = static_cast<size_t>(atoi(argv[index + 1]));
				index += 2;
			}
			else if(!strcmp(argv[index], "--pageSize") && index + 1 < argc)
			{
				_realBlockSizeBit = static_cast<size_t>(atoi(argv[index + 1]));
				index += 2;
			}
			else
			{
				cerr << "Invalid argument: " << argv[index++] << endl;
			}
		}

		vector<VerificationRange> preUpdateHashes;

		if(!runSchedulerWithFiles(oldFile, newFile, output, preUpdateHashes, wantLog, dryRun))
			return false;

		if(dryRun)
			return true;
		
		return writeVerifRangeToFile(preUpdateHashes, string(output) + ".hashes");
	}
}
