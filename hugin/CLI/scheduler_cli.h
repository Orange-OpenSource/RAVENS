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

#ifndef RAVENS_SCHEDULER_CLI_H
#define RAVENS_SCHEDULER_CLI_H

#ifdef RAVENS_PUBLIC_COMMAND_H
struct VersionData
{
	uint32_t version;
	std::string binaryPath;
	std::string publicKey;
	std::string secretKey;
	std::string manifest1Path;
	std::string manifest2Path;

	std::vector<VerificationRange> rangesToCheckBeforeUpdate;
	uint32_t startVerificationIndex;
};
#endif

void printSchedulerHelp();
bool processScheduler(int argc, char *argv[]);

void printAuthenticationHelp();
bool processAuthentication(int argc, char *argv[]);

#ifdef RAVENS_PUBLIC_COMMAND_H
	bool runSchedulerWithFiles(const char * oldFile, const char * newFile, const char * output, std::vector<VerificationRange> & preUpdateHashes, bool printLog, bool dryRun);
	bool processSchedulerBatch(const char * configFile, char * outputDir);
	bool parseConfig(const char * configFile, bool wantManifests, std::vector<VersionData> & output, size_t & flashSize, size_t & flashPageSize);
#endif

bool canUseDir(char * dir);

#endif //RAVENS_SCHEDULER_CLI_H
