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
#include <vector>
#include <algorithm>
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/filewritestream.h>
#include <rapidjson/error/en.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include "../Scheduler/bsdiff/bsdiff.h"
#include "../Scheduler/config.h"
#include "../Scheduler/public_command.h"
#include "scheduler_cli.h"

using namespace std;

bool parseConfig(const char * configFile, bool wantManifests, vector<VersionData> & output, size_t & flashSize, size_t & flashPageSize)
{
	size_t configSize;
	uint8_t * configContent = readFile(configFile, &configSize);
	if(configContent == nullptr)
	{
		cerr << "Couldn't load the config file" << endl;
		return false;
	}

	rapidjson::Document config;
	rapidjson::ParseResult ok = config.Parse((const char *) configContent, configSize);
	if(!ok)
	{
		cerr << "Invalid config file: couldn't parse the JSON format: " <<  rapidjson::GetParseError_En(ok.Code()) << " Offset " << to_string(ok.Offset()) << endl;
		free(configContent);
		return false;
	}

	free(configContent);

	if(!config.IsObject())
	{
		cerr << "Invalid config format" << endl;
		return false;
	}

	if(config.HasMember("flashSizeBits") && config["flashSizeBits"].IsUint())
		flashSize = config["flashSizeBits"].GetUint();
	else
		flashSize = FLASH_SIZE_BIT_DEFAULT;

	if(config.HasMember("flashPageSizeBits") && config["flashPageSizeBits"].IsUint())
		flashPageSize = config["flashPageSizeBits"].GetUint();
	else
		flashPageSize = BLOCK_SIZE_BIT_DEFAULT;

	if(flashPageSize > flashSize)
	{
		cerr << "Page size can't be larger than flash size" << endl;
		return false;
	}

	if(flashSize > sizeof(size_t) * 8)
	{
		cerr << "Flash size is too large for this computer to handle (max is " << sizeof(size_t) * 8 << " bits)" << endl;
		return false;
	}

	if(!config.HasMember("versions") || !config["versions"].IsArray())
	{
		cerr << "Invalid config format: versions is missing" << endl;
		return false;
	}

	const auto & versions = config["versions"].GetArray();
	uint8_t countWithoutManifest2 = 0;
	uint32_t versionWithoutManifest2 = 0, largestVersionMet = 0;

	for(const auto &version : versions)
	{
		if(!version.IsObject() || !version.HasMember("versionID") || !version.HasMember("binary")
		   || !version["versionID"].IsUint() || !version["binary"].IsString())
		{
			cerr << "Invalid config format: invalid version format" << endl;
			return false;
		}

		vector<VerificationRange> rangesToCheckBeforeUpdate;

		if(version.HasMember("ranges") && version["ranges"].IsArray())
		{
			for(const auto & range : version["ranges"].GetArray())
			{
				if(!range.HasMember("start") || !range["start"].IsUint() ||
						!range.HasMember("length") || !range["length"].IsUint() ||
						!range.HasMember("hash") || !range["hash"].IsString())
				{
					cerr << "Invalid range to validate" << endl;
					return false;
				}

				VerificationRange newRange(range["start"].GetUint(), static_cast<uint16_t>(range["length"].GetUint()));
				newRange.expectedHash = string(range["hash"].GetString());

				rangesToCheckBeforeUpdate.push_back(newRange);
			}
		}

		const uint32_t currentVersion = version["versionID"].GetUint();

		if(wantManifests)
		{
			if(!version.HasMember("manifest2") || !version["manifest2"].IsString())
			{
				//One version (the largest doesn't have a manifest 2)
				if(countWithoutManifest2 == 0)
				{
					countWithoutManifest2 += 1;
					versionWithoutManifest2 = version["versionID"].GetUint();

					output.emplace_back(VersionData {
							.version = currentVersion,
							.binaryPath = version["binary"].GetString(),
							.publicKey = "",
							.secretKey = "",
							.manifest1Path = "",
							.manifest2Path = "",
							.rangesToCheckBeforeUpdate = rangesToCheckBeforeUpdate,
							.startVerificationIndex = 0
					});

				}
				else
				{
					cerr << "Invalid config format: missing manifest path to versions" << endl;
					return false;
				}
			}
			else
			{
				output.emplace_back(VersionData {
						.version = currentVersion,
						.binaryPath = version["binary"].GetString(),
						.publicKey = "",
						.secretKey = "",
						.manifest1Path = "",
						.manifest2Path = version["manifest2"].GetString(),
						.rangesToCheckBeforeUpdate = rangesToCheckBeforeUpdate,
						.startVerificationIndex = 0
				});
			}
		}
		else
		{
			output.emplace_back(VersionData {
					.version = currentVersion,
					.binaryPath = version["binary"].GetString(),
					.publicKey = "",
					.secretKey = "",
					.manifest1Path = "",
					.manifest2Path = "",
					.rangesToCheckBeforeUpdate = rangesToCheckBeforeUpdate,
					.startVerificationIndex = 0
			});
		}

		if(currentVersion > largestVersionMet)
			largestVersionMet = currentVersion;
	}

	if(countWithoutManifest2 != 0 && versionWithoutManifest2 != largestVersionMet)
	{
		cerr << "Invalid config format: missing manifest path to version" << to_string(versionWithoutManifest2) << endl;
		return false;
	}

	//Sort the versions we read by ascending versionID
	if(output.size() > 1)
		sort(output.begin(), output.end(), [](const VersionData & a, const VersionData & b) {	return a.version < b.version;	});

	return true;
}

bool mkpath(char* path, mode_t mode)
{
	if(path == nullptr || *path == 0)
		return false;

	for (char * p = strchr(&path[1], '/'); p; p = strchr(p + 1, '/'))
	{
		*p='\0';
		if (mkdir(path, mode)==-1)
		{
			if (errno != EEXIST)
			{
				*p='/';
				return false;
			}
		}
		*p='/';
	}
	return true;
}

bool canUseDir(char * dir)
{
	if(dir == nullptr)
		return false;

	size_t length = strlen(dir);
	if(length == 0)
		return false;

	if(dir[length - 1] != '/')
	{
		char * newDir = (char*) malloc(length + 1);
		if(newDir == nullptr)
			return false;

		memcpy(newDir, dir, length);
		newDir[length] = '/';
		newDir[length + 1] = 0;

		bool retValue = canUseDir(newDir);

		free(newDir);

		return retValue;
	}

	//Does the file/dir exist?
	struct stat info = {};
	if(stat(dir, &info) != 0)
	{
		return mkpath(dir, 0755);
	}

	bool isDir = (info.st_mode & S_IFDIR) != 0;

	if(isDir)
	{
		DIR *directory = opendir(dir);
		if(directory == nullptr)
			return false;

		struct dirent *entry;
		while ((entry = readdir(directory)) != nullptr)
		{
			if(!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
				continue;

			cout << "The output directory (" << dir << ") is not empty." << endl << "Ignore (at the risk of overwritting files) ? (y/n) ";

			char c;
			while((c = getc(stdin)) != 'y' && c != 'n');

			//Allowed to potentially overwrite files
			return c == 'y';
		}

		return true;
	}
	else
	{
		cout << "A file already exist with the same path as the output directory (" << dir << ")" << endl << "Delete ? (y/n)";

		char c;
		while((c = getc(stdin)) != 'y' && c != 'n');

		//Not allowed to delete the file
		if(c != 'y')
			return false;

		remove(dir);
		return mkpath(dir, 0755);
	}
}

bool processSchedulerBatch(const char * configFile, char * outputDir)
{
	size_t flashSize, flashPageSize;
	vector<VersionData> versions;
	if(!parseConfig(configFile, false, versions, flashSize, flashPageSize))
		return false;

	//Update the value
	_realBlockSizeBit = flashPageSize;
	_realFullAddressSpace = flashSize;

	if(versions.size() < 2)
	{
		cerr << "No binary to diff with" << endl;
		return false;
	}

	//There is at least two values, and we want 0 duplicates
	uint32_t lastVersion = versions.back().version;
	for(const auto & version : versions)
	{
		if(lastVersion == version.version)
		{
			cerr << "Invalid config file: Duplicate versionID" << endl;
			return false;
		}

		lastVersion = version.version;
	}

	//Make sure we can freely write to the output path
	if(!canUseDir(outputDir))
	{
		cerr << "Couldn't take control of the output directory" << endl;
		return false;
	}

	//Create a new config file
	rapidjson::Document outputConfig;
	outputConfig.SetObject();

	//Write back custom values
	if(FLASH_SIZE_BIT != FLASH_SIZE_BIT_DEFAULT)
	{
		rapidjson::Value flashSizeValue;
		flashSizeValue.SetUint(static_cast<unsigned int>(flashSize));
		outputConfig.AddMember("flashSizeBits", flashSizeValue, outputConfig.GetAllocator());
	}

	if(BLOCK_SIZE_BIT != BLOCK_SIZE_BIT_DEFAULT)
	{
		rapidjson::Value flashSizeValue;
		flashSizeValue.SetUint(static_cast<unsigned int>(flashSize));
		outputConfig.AddMember("flashPageSizeBits", flashSizeValue, outputConfig.GetAllocator());
	}

	rapidjson::Value configVersionArray;
	configVersionArray.SetArray();

	//Extract the last version toward which we have to diff
	VersionData finalVersion = versions.back();
	versions.pop_back();

	for(const auto & oldVersion : versions)
	{
		//Craft the output file name
		const string output("manifest2_" + to_string(oldVersion.version) + "_" + to_string(finalVersion.version));
		const string fullOutput = string(outputDir) + "/" + output;
		vector<VerificationRange> preUpdateHashes;

		//Generate the manifest
		if(!runSchedulerWithFiles(oldVersion.binaryPath.c_str(), finalVersion.binaryPath.c_str(), fullOutput.c_str(), preUpdateHashes, false))
		{
			cerr << "Couldn't diff with version " << to_string(oldVersion.version) << " (file " << oldVersion.binaryPath << ")" << endl;
			return false;
		}

		//Create the new object in the output JSON file
		rapidjson::Value versionID, binaryPath, manifestPath;

		versionID.SetUint(oldVersion.version);
		binaryPath.SetString(oldVersion.binaryPath.c_str(), static_cast<rapidjson::SizeType>(oldVersion.binaryPath.size()), outputConfig.GetAllocator());
		manifestPath.SetString(output.c_str(), static_cast<rapidjson::SizeType>(output.size()), outputConfig.GetAllocator());

		rapidjson::Value currentVersion;
		currentVersion.SetObject();

		currentVersion.AddMember("versionID", versionID, outputConfig.GetAllocator());
		currentVersion.AddMember("binary", binaryPath, outputConfig.GetAllocator());
		currentVersion.AddMember("manifest2", manifestPath, outputConfig.GetAllocator());

		//If we have ranges to check
		if(!preUpdateHashes.empty())
		{
			rapidjson::Value verificationArray;
			verificationArray.SetArray();

			//Add a range to the array
			for(const auto & verif : preUpdateHashes)
			{
				rapidjson::Value startRange, lengthRange, expectedHash, currentCheck;

				startRange.SetUint(verif.start);
				lengthRange.SetUint(verif.length);
				expectedHash.SetString(verif.expectedHash.c_str(), static_cast<rapidjson::SizeType>(verif.expectedHash.size()), outputConfig.GetAllocator());

				currentCheck.SetObject();
				currentCheck.AddMember("start", startRange, outputConfig.GetAllocator());
				currentCheck.AddMember("length", lengthRange, outputConfig.GetAllocator());
				currentCheck.AddMember("hash", expectedHash, outputConfig.GetAllocator());

				verificationArray.PushBack(currentCheck, outputConfig.GetAllocator());
			}

			currentVersion.AddMember("ranges", verificationArray, outputConfig.GetAllocator());
		}

		configVersionArray.PushBack(currentVersion, outputConfig.GetAllocator());
	}

	//Append the active version
	rapidjson::Value versionID, binaryPath, manifestPath;

	versionID.SetUint(finalVersion.version);
	binaryPath.SetString(finalVersion.binaryPath.c_str(), static_cast<rapidjson::SizeType>(finalVersion.binaryPath.size()), outputConfig.GetAllocator());

	rapidjson::Value currentVersion;
	currentVersion.SetObject();

	currentVersion.AddMember("versionID", versionID, outputConfig.GetAllocator());
	currentVersion.AddMember("binary", binaryPath, outputConfig.GetAllocator());

	configVersionArray.PushBack(currentVersion, outputConfig.GetAllocator());
	outputConfig.AddMember("versions", configVersionArray, outputConfig.GetAllocator());

	//Write the new config to disk
	FILE * outputConfigFile = fopen(string(string(outputDir) + "/config.json").c_str(), "w+");
	if(outputConfigFile == nullptr)
	{
		cerr << "Couldn't write new config file" << endl;
		return false;
	}

	char writeBuffer[0x10000];
	rapidjson::FileWriteStream os(outputConfigFile, writeBuffer, sizeof(writeBuffer));
	rapidjson::PrettyWriter<rapidjson::FileWriteStream> writer(os);
	outputConfig.Accept(writer);
	fclose(outputConfigFile);

	return true;
}

