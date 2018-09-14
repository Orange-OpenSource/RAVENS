//
// Created by Emile-Hugo Spir on 5/2/18.
//

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/param.h>

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
typedef unsigned int uint;

#include "../io_management.h"
#include <layout.h>
#include "network.h"
#include "userland.h"
#include "../validation.h"
#include "../core.h"
#include "../driver_api.h"

const uint8_t __attribute__((section(".rodata.Hermes.storageRoom"), aligned(BLOCK_SIZE))) updateStorage[16 * BLOCK_SIZE] = {0xff};	//65kB of room.

void sendRequest()
{
	char version[sizeof(criticalMetadata.versionID) * 3 + 1];

	uint length = sizeof(BASE_REQUEST) - 1;
	char request[sizeof(criticalMetadata.updateChallenge) * 2 + sizeof(version)
				 + sizeof(BASE_REQUEST) + sizeof(SEC_REQUEST) + sizeof(FINAL_REQUEST)];

	strcpy(request, BASE_REQUEST);

	itoa(criticalMetadata.versionID, version, 10);
	uint8_t versionLength = (uint8_t) strlen(version);

	strcpy(&request[length], version);
	length += versionLength;

	strcpy(&request[length], SEC_REQUEST);
	length += sizeof(SEC_REQUEST) - 1;

	length += base64_encode((const uint8_t *) criticalMetadata.updateChallenge, sizeof(criticalMetadata.updateChallenge), &request[length]);

	strcpy(&request[length], FINAL_REQUEST);
	length += sizeof(FINAL_REQUEST);

	proxyNewRequest(UPDATE_SERVER, UPDATE_SERVER_PORT, request, length);
}

void processValidationRequest(UpdateHashRequest * extraValidation, char ** response, uint32_t * responseLength)
{
	//Check the signature
	if(validateExtraValidation(extraValidation))
	{
		//We hash the various segments that were requested and append them to the response buffer
		*response = malloc((size_t) (extraValidation->numberValidation * HASH_LENGTH) + 1);
		if(*response != NULL)
		{
			for(uint16_t i = 0; i < extraValidation->numberValidation; ++i)
			{
				uint8_t hash[HASH_LENGTH];
				hashMemory((const uint8_t *) (uintptr_t) extraValidation->validateSegment[i].start, extraValidation->validateSegment[i].length + 1, hash);
				memcpy(&(*response)[i * HASH_LENGTH], hash, sizeof(hash));
			}

			*responseLength = extraValidation->numberValidation * HASH_LENGTH;
			(*response)[*responseLength] = 0;
		}
	}
}

void sendManifest2Request(const char * validationString, uint32_t validationStringLength)
{
	char version[sizeof(criticalMetadata.versionID) * 3 + 1];
	itoa(criticalMetadata.versionID, version, 10);
	uint8_t versionLength = (uint8_t) strlen(version);

	uint length = sizeof(MAN2_REQUEST) - 1;
	char request[sizeof(MAN2_REQUEST) + versionLength + sizeof(MAN2_SEC_REQUEST) + sizeof(version) + sizeof(FINAL_REQUEST) + validationStringLength + 1];

	strcpy(request, MAN2_REQUEST);

	strcpy(&request[length], version);
	length += versionLength;

	strcpy(&request[length], MAN2_SEC_REQUEST);
	length += sizeof(MAN2_SEC_REQUEST) - 1;

	itoa(validationStringLength, version, 10);
	strcpy(&request[length], version);
	length += strlen(version);

	strcpy(&request[length], FINAL_REQUEST);
	length += sizeof(FINAL_REQUEST) - 1;

	memcpy(&request[length], validationString, validationStringLength);
	length += validationStringLength;
	request[length] = 0;

	proxyNewRequest(UPDATE_SERVER, UPDATE_SERVER_PORT, request, length);
}

uint32_t getVersion()
{
	return criticalMetadata.versionID;
}

void checkUpdate()
{
	sendRequest();

	//Wait for data
	uint8_t networkStatus;
	uint32_t length = 0;
	do
	{
		networkStatus = proxyRequestHasData();
		if(networkStatus == NETWORK_NO_DATA)
		{
			usleep(50);
			continue;
		}
		else if(networkStatus == NETWORK_HAS_DATA)
		{
			const NetworkStreamingData * data = proxyRequestGetData();

			if(data == NULL)
				return proxyCloseSession();

			const uint32_t localLength = data->length;

			if(data->buffer == NULL || localLength > sizeof(cacheRAM) - length)
				return proxyCloseSession();

			//Copy the reply to a safer buffer
			memcpy(&cacheRAM[length], data->buffer, localLength);
			length += localLength;
		}

	} while(networkStatus != NETWORK_FINISHED);

	//Do we have something invalid?

	//Get the data and verify we have at least a valid HTTP reply
	if(length <= sizeof(EXPECTED_REPLY_UPDATE) || strncmp((const char *) cacheRAM, EXPECTED_REPLY_UPDATE, sizeof(EXPECTED_REPLY_UPDATE) - 1) != 0)
	{
		return proxyCloseSession();
	}

	proxyReleaseData();

	for(uint16_t i = 0; i < length; ++i)
		cacheRAM[i] = cacheRAM[sizeof(EXPECTED_REPLY_UPDATE) - 1 + i];

	//Validate the header
	UpdateHeader * header = (UpdateHeader *) cacheRAM;
	if(!validateHeader(header))
		return proxyCloseSession();

	char * challengeResponse = NULL;
	uint32_t  challengeResponseLength = 0;

	if(header->sectionSignedDeviceKey.haveExtra)
	{
		UpdateHashRequest * extraValidation = (UpdateHashRequest *) (cacheRAM + sizeof(UpdateHeader));

		//We check the validation wasn't truncated
		if(length - sizeof(UpdateHeader) >= extraValidation->numberValidation * sizeof(extraValidation->validateSegment[0]) + sizeof(extraValidation->numberValidation))
			processValidationRequest(extraValidation, &challengeResponse, &challengeResponseLength);
	}

	//Send the request. Meanwhile, we start writing the header
	sendManifest2Request(challengeResponse, challengeResponseLength);

	//Flush the space necessary to write the update
	eraseNecessarySpace((const void *) updateStorage, sizeof(UpdateHeader) + header->sectionSignedDeviceKey.manifestLength);

	//Write the update header
	size_t writePosition = (size_t) updateStorage;

	uint16_t httpReplyLeft = sizeof(EXPECTED_REPLY_UPDATE) - 1, bufferPos = sizeof(UpdateHeader);
	size_t manifestLength = header->sectionSignedDeviceKey.manifestLength;

	while((networkStatus = proxyRequestHasData()) != NETWORK_FINISHED)
	{
		if(networkStatus == NETWORK_HAS_DATA)
		{
			const NetworkStreamingData * data = proxyRequestGetData();
			if(data == NULL || data->buffer == NULL || data->length == 0)
				continue;

			uint32_t dataLength = data->length, currentInputBufferPos = 0;
			const uint8_t * buffer = data->buffer;

			//Remove the HTTP reply
			if(httpReplyLeft != 0)
			{
				if(dataLength >= httpReplyLeft)
				{
					if(strncmp((const char *) buffer, EXPECTED_REPLY_UPDATE + (sizeof(EXPECTED_REPLY_UPDATE) - 1 - httpReplyLeft), httpReplyLeft) != 0)
						return proxyCloseSession();

					dataLength -= httpReplyLeft;
					buffer = &buffer[httpReplyLeft];
					httpReplyLeft = 0;

					if(dataLength == 0)
						continue;
				}
				else
				{
					if(strncmp((const char *) buffer, EXPECTED_REPLY_UPDATE + (sizeof(EXPECTED_REPLY_UPDATE) - 1 - httpReplyLeft), dataLength) != 0)
						return proxyCloseSession();

					httpReplyLeft -= data->length;
					continue;
				}
			}

			if(dataLength > manifestLength)
				break;

			//Copy the input data in the cacheRAM buffer page by page so that writes are properly aligned
			do
			{
				const uint32_t spaceLeftInInputBuffer = dataLength - currentInputBufferPos;
				const uint16_t spaceLeftInCache = (uint16_t) (BLOCK_SIZE - bufferPos);

				if(spaceLeftInInputBuffer < spaceLeftInCache)
				{
					memcpy(&cacheRAM[bufferPos], &buffer[currentInputBufferPos], dataLength);
					bufferPos += dataLength;
					currentInputBufferPos += spaceLeftInInputBuffer;
				}
				else
				{
					memcpy(&cacheRAM[bufferPos], &buffer[currentInputBufferPos], spaceLeftInCache);
					erasePage(writePosition);
					writeToNAND(writePosition, BLOCK_SIZE, cacheRAM);
					writePosition += BLOCK_SIZE;

					bufferPos = 0;
					currentInputBufferPos += spaceLeftInCache;
				}

			} while(currentInputBufferPos < dataLength);

			manifestLength -= dataLength;
		}
		else
			usleep(50);
	}

	proxyCloseSession();

	//Some data left in the cache to commit
	if(bufferPos != 0)
	{
		erasePage(writePosition);
		writeToNAND(writePosition, BLOCK_SIZE, cacheRAM);
	}

	//We downloaded the full payload
	if(manifestLength == 0)
	{
		requestUpdate(updateStorage);
		reboot();
	}
}
