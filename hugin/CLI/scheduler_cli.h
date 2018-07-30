//
// Created by Emile-Hugo Spir on 5/14/18.
//

#ifndef HERMES_SCHEDULER_CLI_H
#define HERMES_SCHEDULER_CLI_H

#ifdef HERMES_PUBLIC_COMMAND_H
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

#ifdef HERMES_PUBLIC_COMMAND_H
	bool runSchedulerWithFiles(const char * oldFile, const char * newFile, const char * output, std::vector<VerificationRange> & preUpdateHashes, bool printLog);
	bool processSchedulerBatch(const char * configFile, char * outputDir);
	bool parseConfig(const char * configFile, bool wantManifests, std::vector<VersionData> & output, size_t & flashSize, size_t & flashPageSize);
#endif

bool canUseDir(char * dir);

#endif //HERMES_SCHEDULER_CLI_H
