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
#include <cstring>
#include <vector>
#include <sys/stat.h>
#include "../Scheduler/public_command.h"
#include "scheduler_cli.h"
#include <libhydrogen/hydrogen.h>
#include "../Scheduler/config.h"
#include <layout.h>
#include <crypto.h>
#include <rapidjson/document.h>
#include <rapidjson/filewritestream.h>
#include <rapidjson/prettywriter.h>

using namespace std;

void printAuthenticationHelp()
{
	cout << endl << "The authenticate option lets you sign patches and generate the key necessary to Odin." << endl;
	cout << "This option require access to the device private key." << endl;
	cout << "Therefore, this should ONLY be called on a secure, isolated server (hopefully with an HSM involved)."<< endl;
	cout << "The diff and authenticate options are separated precisely to enforce this separation." << endl << endl;

	cout << "Mandatory arguments:" << endl <<
		 "	[--path | -p] path	- Path to the directory containing the config and the manifest files." << endl <<
		 "	[--output | -o] outputDirectory" << endl <<
		 "	[--key | -k] pathToPrivateDeviceKey" << endl << endl;
}

bool authenticate(vector<VersionData> & versions, const char * inputPath, const char * keyFile, const char * outputPath)
{
	const VersionData lastVersion = versions.back();

	//Can the version fit in 31 bits
	if(lastVersion.version > (1u << 31u))
	{
		cerr << "Version ID is too large to encode in the manifest." << endl;
		return false;
	}

	uint8_t privateKey[hydro_sign_SECRETKEYBYTES];
	if(!loadKey(keyFile, true, privateKey))
	{
		cerr << "Couldn't load the device key" << endl;
		return false;
	}

	const string inputString(inputPath), output(outputPath);

	for(auto & version : versions)
	{
		if(version.version == lastVersion.version)
			continue;

		const string inputManifest(inputString + "/" + version.manifest2Path);

		UpdateHeader manifest1;
		memset(&manifest1, 0, sizeof(manifest1));
		manifest1.sectionSignedDeviceKey.formatVersion = MANIFEST_FORMAT_VERSION;

		//Get manifest2 size
		struct stat st;
		if(stat(inputManifest.c_str(), &st) != 0)
		{
			cerr << "Couldn't get metadata on " << version.manifest2Path << ". Aborting" << endl;
			clearMemory(privateKey, sizeof(privateKey));
			return false;
		}
		else if(st.st_size == 0 || st.st_size > UINT32_MAX)
		{
			cerr << "Invalid manifest size for " << version.manifest2Path << ". Aborting" << endl;
			clearMemory(privateKey, sizeof(privateKey));
			return false;
		}

		//Hash the manifest2
		if(!hashFile(inputManifest.c_str(), manifest1.sectionSignedDeviceKey.updateHash, 0))
		{
			cerr << "Couldn't hash " << version.manifest2Path << ". Aborting" << endl;
			clearMemory(privateKey, sizeof(privateKey));
			return false;
		}

		//Populate some fields
		manifest1.sectionSignedDeviceKey.manifestLength = (uint32_t) st.st_size;
		manifest1.sectionSignedDeviceKey.oldVersionID = version.version;
		manifest1.sectionSignedDeviceKey.versionID = lastVersion.version;
		manifest1.sectionSignedDeviceKey.haveExtra = !version.rangesToCheckBeforeUpdate.empty();

		size_t bufferLength = 0;
		uint8_t * hashVerificationBuffer = nullptr;

		if(manifest1.sectionSignedDeviceKey.haveExtra)
		{
			//Check that we can safely encode the number of validations
			assert(version.rangesToCheckBeforeUpdate.size() < UINT16_MAX);

			//Grab a buffer
			bufferLength = SIGNATURE_LENGTH + sizeof(uint16_t) + version.rangesToCheckBeforeUpdate.size() * sizeof(struct SingleHashRequest);
			hashVerificationBuffer = (uint8_t *) malloc(bufferLength);
			if(hashVerificationBuffer == nullptr)
			{
				cerr << "Memory error!" << endl;
				clearMemory(privateKey, sizeof(privateKey));
				return false;
			}

			size_t bufferIndex = SIGNATURE_LENGTH;

			//Add the numberValidation field of UpdateHashRequest
			auto numberValidation = static_cast<uint16_t>(version.rangesToCheckBeforeUpdate.size());
			memcpy(&hashVerificationBuffer[bufferIndex], &numberValidation, sizeof(numberValidation));
			bufferIndex += sizeof(numberValidation);

			//Append SingleHashRequest
			for(uint16_t i = 0; i < numberValidation; ++i)
			{
				SingleHashRequest curHashRequest{
					.start = version.rangesToCheckBeforeUpdate[i].start,
					.length = version.rangesToCheckBeforeUpdate[i].length
				};

				memcpy(&hashVerificationBuffer[bufferIndex], &curHashRequest, sizeof(curHashRequest));
				bufferIndex += sizeof(curHashRequest);
			}

			//Alright, we can sign the package
			signBuffer(hashVerificationBuffer + SIGNATURE_LENGTH,
					   bufferLength - SIGNATURE_LENGTH,
					   hashVerificationBuffer, privateKey);
		}

		//We're now only missing the public key and the signature for the main package

		//Generate the keys
		uint8_t secretUpdateKey[hydro_sign_SECRETKEYBYTES];
		generateKeyMemory(secretUpdateKey, manifest1.sectionSignedDeviceKey.updatePubKey);

		//Sign the package
		signBuffer((const uint8_t *) &manifest1.sectionSignedDeviceKey + SIGNATURE_LENGTH,
				   sizeof(manifest1.sectionSignedDeviceKey) - sizeof(manifest1.sectionSignedDeviceKey.signature),
				   manifest1.sectionSignedDeviceKey.signature, privateKey);

		//Save the public & private keys
		const string versionString("_" + to_string(version.version) + "_" + to_string(lastVersion.version));

		version.manifest1Path = "manifest1";
		version.manifest1Path += versionString;

		const string newManifest1Path = output + '/' + version.manifest1Path;

		{
			char secretKeyHex[hydro_sign_SECRETKEYBYTES * 2 + 1];
			hydro_bin2hex(secretKeyHex, sizeof(secretKeyHex), secretUpdateKey, sizeof(secretUpdateKey));
			version.secretKey = string(secretKeyHex);

			clearMemory(secretUpdateKey, sizeof(secretUpdateKey));
			clearMemory((uint8_t *) secretKeyHex, sizeof(secretKeyHex));
		}

		{
			char publicKeyHex[hydro_sign_PUBLICKEYBYTES * 2 + 1] = {0};
			hydro_bin2hex(publicKeyHex, sizeof(publicKeyHex), manifest1.sectionSignedDeviceKey.updatePubKey, sizeof(manifest1.sectionSignedDeviceKey.updatePubKey));
			version.publicKey = string(publicKeyHex);
		}

		//Write the manifest 1
		FILE * outputFile = fopen(newManifest1Path.c_str(), "wb");
		if(outputFile == nullptr)
		{
			clearMemory(privateKey, sizeof(privateKey));
			cerr << "Couldn't open the file for the manifest 1 (" << newManifest1Path << ")" << endl;
			free(hashVerificationBuffer);
			return false;
		}

		if(fwrite(&manifest1, 1, sizeof(manifest1.sectionSignedDeviceKey), outputFile) != sizeof(manifest1.sectionSignedDeviceKey))
		{
			clearMemory(privateKey, sizeof(privateKey));
			cerr << "Couldn't write to the manifest 1 file (" << newManifest1Path << ")" << endl;
			free(hashVerificationBuffer);
			return false;
		}

		if(manifest1.sectionSignedDeviceKey.haveExtra)
		{
			version.startVerificationIndex = sizeof(manifest1.sectionSignedDeviceKey);
			if(fwrite(hashVerificationBuffer, 1, bufferLength, outputFile) != bufferLength)
			{
				clearMemory(privateKey, sizeof(privateKey));
				cerr << "Couldn't write to the validation portion of the manifest 1 file (" << newManifest1Path << ")" << endl;
				free(hashVerificationBuffer);
				return false;
			}
		}
		else
			version.startVerificationIndex = 0;

		free(hashVerificationBuffer);
		fclose(outputFile);

		const string newManifest2Path(output + "/" + version.manifest2Path);

		//We generated the manifest 1. Now, we may move the manifest 2 to the final directory
		if(rename(inputManifest.c_str(), newManifest2Path.c_str()))
		{
			clearMemory(privateKey, sizeof(privateKey));
			cerr << "Couldn't move the manifest 2 of version " << to_string(version.version) << " to it's new path (" << newManifest2Path << ")" << endl;
			return false;
		}
	}

	return true;
}

bool writeJson(vector<VersionData> & versions, const char * outputPath)
{
	string output(outputPath);
	output += "/update.json";

	VersionData lastVersion = versions.back();
	versions.pop_back();

	rapidjson::Document outputConfig;
	outputConfig.SetObject();

	rapidjson::Value curVersion;
	curVersion.SetUint(lastVersion.version);
	outputConfig.AddMember("currentVersion", curVersion, outputConfig.GetAllocator());

	rapidjson::Value versionArray;
	versionArray.SetObject();

	for(const auto & version : versions)
	{
		rapidjson::Value chunck;
		chunck.SetObject();

		rapidjson::Value publicKey, privateKey, manifest1, manifest2;

		publicKey.SetString(version.publicKey.c_str(), static_cast<rapidjson::SizeType>(version.publicKey.size()));
		privateKey.SetString(version.secretKey.c_str(), static_cast<rapidjson::SizeType>(version.secretKey.size()));
		manifest1.SetString(version.manifest1Path.c_str(), static_cast<rapidjson::SizeType>(version.manifest1Path.size()));
		manifest2.SetString(version.manifest2Path.c_str(), static_cast<rapidjson::SizeType>(version.manifest2Path.size()));

		chunck.AddMember("publicKey", publicKey, outputConfig.GetAllocator());
		chunck.AddMember("privateKey", privateKey, outputConfig.GetAllocator());
		chunck.AddMember("manifest1", manifest1, outputConfig.GetAllocator());
		chunck.AddMember("manifest2", manifest2, outputConfig.GetAllocator());

		if(!version.rangesToCheckBeforeUpdate.empty())
		{
			rapidjson::Value validation, validationIndex;
			validation.SetArray();

			for(const auto & range : version.rangesToCheckBeforeUpdate)
			{
				rapidjson::Value hash;
				hash.SetString(range.expectedHash.c_str(), static_cast<rapidjson::SizeType>(range.expectedHash.size()));
				validation.PushBack(hash, outputConfig.GetAllocator());
			}

			assert(version.startVerificationIndex != 0);

			validationIndex.SetUint(version.startVerificationIndex);

			chunck.AddMember("validation", validation, outputConfig.GetAllocator());
			chunck.AddMember("validationIndex", validationIndex, outputConfig.GetAllocator());
		}

		string versionString(to_string(version.version));
		versionArray.AddMember(rapidjson::StringRef(versionString.c_str(), versionString.size()), chunck, outputConfig.GetAllocator());
	}

	outputConfig.AddMember("payload", versionArray, outputConfig.GetAllocator());

	//Write to disk
	FILE * outputConfigFile = fopen(output.c_str(), "w+");
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

	for(auto & version : versions)
		fill(version.secretKey.begin(), version.secretKey.end(), 0);

	return true;
}

bool processAuthentication(int argc, char *argv[])
{
	int index = 1;
	char * output = nullptr;
	const char * path = nullptr, * keyFile = nullptr;

	while(index < argc)
	{
		if((!strcmp(argv[index], "--path") || !strcmp(argv[index], "-p")) && index + 1 < argc)
		{
			path = argv[index + 1];
			index += 2;
		}
		else if((!strcmp(argv[index], "--key") || !strcmp(argv[index], "-k")) && index + 1 < argc)
		{
			keyFile = argv[index + 1];
			index += 2;
		}
		else if((!strcmp(argv[index], "--output") || !strcmp(argv[index], "-o")) && index + 1 < argc)
		{
			output = argv[index + 1];
			index += 2;
		}
		else
		{
			cerr << "Invalid argument: " << argv[index++] << endl;
		}
	}

	if(output == nullptr || path == nullptr || keyFile == nullptr)
	{
		cerr << "Missing arguments." << endl;
		return false;
	}

	if(!canUseDir(output))
		return false;

	size_t ignore;
	const string pathToConfig(string(path) + "/config.json");
	vector<VersionData> versions;

	if(!parseConfig(pathToConfig.c_str(), true, versions, ignore, ignore))
		return false;

	if(!authenticate(versions, path, keyFile, output))
		return false;

	return writeJson(versions, output);
}
