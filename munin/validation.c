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

#include <stdbool.h>
#include <stdint.h>
#include <memory.h>

#include "io_management.h"

#ifdef TARGET_LIKE_MBED
	#include "decoder.h"
#else
	#include <decoding/decoder.h>
#endif

#include "../common/layout.h"
#include "validation.h"
#include "core.h"

RAVENS_CRITICAL bool validateSectionSignedWithDeviceKey(const UpdateHeader * header)
{
	if(header->sectionSignedDeviceKey.formatVersion != criticalMetadata.formatVersionCompatibility)
		return false;

	return validateSignature((const uint8_t *) header + SIGNATURE_LENGTH,
									sizeof(header->sectionSignedDeviceKey) - SIGNATURE_LENGTH,
									header->sectionSignedDeviceKey.signature,
									criticalMetadata.devicePublicKey);
}

RAVENS_CRITICAL bool validateSectionSignedWithUpdateKey(const UpdateHeader * header)
{
	//Authenticate the challenge reply. The signature is performed on the following data: Challenge + vID + updateKey + random
	uint8_t challenge[sizeof(criticalMetadata.updateChallenge) + sizeof(header->sectionSignedDeviceKey.oldVersionID) + sizeof(uint32_t) + sizeof(header->sectionSignedDeviceKey.updatePubKey) + sizeof(header->sectionSignedUpdateKey.random)];

	//Append the update challenge
	memcpy(challenge, criticalMetadata.updateChallenge, sizeof(criticalMetadata.updateChallenge));

	//Append the old version ID
	memcpy(&challenge[sizeof(criticalMetadata.updateChallenge)], &header->sectionSignedDeviceKey.oldVersionID, sizeof(header->sectionSignedDeviceKey.oldVersionID));

	//Append the new version ID
	const uint32_t version = header->sectionSignedDeviceKey.versionID;
	memcpy(&challenge[sizeof(criticalMetadata.updateChallenge) + sizeof(header->sectionSignedDeviceKey.oldVersionID)], &version, sizeof(version));

	//Append the update public key
	memcpy(&challenge[sizeof(criticalMetadata.updateChallenge) + sizeof(header->sectionSignedDeviceKey.oldVersionID) + sizeof(version)],
		   header->sectionSignedDeviceKey.updatePubKey, sizeof(header->sectionSignedDeviceKey.updatePubKey));

	//Append the random value
	memcpy(&challenge[sizeof(criticalMetadata.updateChallenge) + sizeof(header->sectionSignedDeviceKey.oldVersionID) + sizeof(version) + sizeof(header->sectionSignedDeviceKey.updatePubKey)],
		   &header->sectionSignedUpdateKey.random, sizeof(header->sectionSignedUpdateKey.random));

	//Check the signature
	return validateSignature(challenge,
								 sizeof(challenge),
								 (const uint8_t *) header->sectionSignedUpdateKey.signature,
								 (const uint8_t *) header->sectionSignedDeviceKey.updatePubKey);
}

RAVENS_CRITICAL bool validateImage(const UpdateHeader * header)
{
	uint8_t hash[HASH_LENGTH];
	hashMemory((const uint8_t *) header + sizeof(UpdateHeader), header->sectionSignedDeviceKey.manifestLength, hash);

	return memcmp(hash, header->sectionSignedDeviceKey.updateHash, HASH_LENGTH) == 0;
}

RAVENS_CRITICAL bool validateHeader(const UpdateHeader * header)
{
	//Authenticate the header sectionSignedDeviceKey with the master key
	if(!validateSectionSignedWithDeviceKey(header))
	{
		return false;
	}

	//Protect against replay attacks by verifying the challenge
	if(!validateSectionSignedWithUpdateKey(header))
	{
		return false;
	}

	//Check that update is appropriate for us
	if(header->sectionSignedDeviceKey.oldVersionID != criticalMetadata.versionID || criticalMetadata.versionID >= header->sectionSignedDeviceKey.versionID)
	{
		return false;
	}

	return true;
}

RAVENS_CRITICAL bool validateExtraValidation(const UpdateHashRequest * request)
{
	return validateSignature((const uint8_t *) request + SIGNATURE_LENGTH,
									request->numberValidation * sizeof(request->validateSegment[0]) + sizeof(request->numberValidation),
									request->signature,
									criticalMetadata.devicePublicKey);
}