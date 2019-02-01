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

#ifndef PERSISTANCE_LAYOUT_H
#define PERSISTANCE_LAYOUT_H

#include "crypto/crypto_utils.h"

#define MANIFEST_FORMAT_VERSION 0
#define BSDIFF_MAGIC 0x5ec1714e

typedef struct __attribute__((__packed__))
{
	struct __attribute__((__packed__))
	{
		uint8_t signature[SIGNATURE_LENGTH];
		uint16_t formatVersion;
		uint8_t updatePubKey[PUBLIC_KEY_LENGTH];
		uint8_t updateHash[HASH_LENGTH];
		uint32_t manifestLength;
		uint32_t oldVersionID;
		uint32_t versionID : 31;
		bool haveExtra : 1;

	} sectionSignedDeviceKey;

	struct __attribute__((__packed__))
	{
		char signature[SIGNATURE_LENGTH];	//Signature imply SIGN(HASH(Challenge + vID + updateKey + random))
		uint64_t random;

	} sectionSignedUpdateKey;
} UpdateHeader;

typedef struct
{
	uint32_t multiplier;
	uint64_t valid; // Should be VALID_64B_VALUE when page is valid
	uint64_t notExpired; // Should be DEFAULT_32B_FLASH_VALUE when page is valid

} UpdateMetadataFooter;

struct __attribute__((__packed__)) SingleHashRequest
{
	uint32_t start;
	uint16_t length;
};

typedef struct
{
	uint8_t signature[SIGNATURE_LENGTH];
	uint16_t numberValidation;
	struct SingleHashRequest validateSegment[];

} UpdateHashRequest;

typedef struct
{
	uint32_t start;
	uint16_t length;
	uint8_t hash[HASH_LENGTH];
} UpdateFinalHash;

#ifdef STATIC_BIT_SIZE

typedef struct
{
	UpdateHeader * location;	//struct update *

	uint8_t bitField[BLOCK_SIZE - sizeof(struct update *) - sizeof(UpdateMetadataFooter)];

	UpdateMetadataFooter footer;

} UpdateMetadata;

typedef struct
{
	uint8_t devicePublicKey[PUBLIC_KEY_LENGTH];
	uint8_t updateChallenge[SIGNATURE_LENGTH];
	uint32_t versionID;
	uint16_t formatVersionCompatibility;

	uint64_t updateInProgress;
	uint64_t valid;

	uint8_t padding[BLOCK_SIZE - (PUBLIC_KEY_LENGTH + SIGNATURE_LENGTH + 3 * sizeof(uint64_t))];

} CriticalMetadata;

extern volatile const CriticalMetadata criticalMetadata;

#endif

#define VALID_64B_VALUE 0x5A5AA5A55A5AA5A5u
//#define DEFAULT_32B_FLASH_VALUE 0xFFFFFFFFu
#define DEFAULT_64B_FLASH_VALUE 0xFFFFFFFFFFFFFFFFu

#endif //PERSISTANCE_LAYOUT_H
