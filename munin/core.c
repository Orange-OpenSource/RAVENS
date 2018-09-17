//
// Created by Emile-Hugo Spir on 4/24/18.
//

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <memory.h>
#include <stdio.h>

typedef unsigned int uint;

#include "Bytecode/execution.h"
#include "core.h"
#include "driver_api.h"
#include "validation.h"
#include "../common/layout.h"
#include "Delta/bsdiff.h"

/*
 * The volatile keyword forces the compiler to hit the flash when accessing a value, instead of aliasing the default value
 *
 * $ let us set the layout order. To make sure CriticalMetadata doesn't share space, we force it between two 0x1000 aligned sections
 * (https://stackoverflow.com/a/6615788)
 */

volatile const UpdateMetadata __attribute__((section(".rodata.Hermes.metadata$1"), aligned(BLOCK_SIZE))) updateMetadataMain = {
		.location = 0,
		.bitField = {[0 ... (sizeof(updateMetadataMain.bitField) - 1)] = 0xff},
		.footer = {
				.multiplier = 0,
				.valid = VALID_64B_VALUE,
				.notExpired = DEFAULT_64B_FLASH_VALUE
		}};

volatile const UpdateMetadata __attribute__((section(".rodata.Hermes.metadata$3"), aligned(BLOCK_SIZE))) updateMetadataSec = {
		.location = 0,
		.bitField = {[0 ... (sizeof(updateMetadataSec.bitField) - 1)] = 0xff},
		.footer = {
				.multiplier = 0,
				.valid = 0,
				.notExpired = 0
		}};

volatile const CriticalMetadata __attribute__((section(".rodata.Hermes.metadata$2"), aligned(BLOCK_SIZE))) criticalMetadata = {
		.devicePublicKey = {0x4d, 0x97, 0x1b, 0x60, 0xfd, 0x83, 0x7f, 0x91, 0xfd, 0x7e, 0xc3, 0x38, 0xc2, 0x80, 0xb7, 0x9c, 0xd1, 0xac, 0x73, 0x60, 0xf7, 0x3c, 0x4c, 0xef, 0xd1, 0x9b, 0x6c, 0xc3, 0x7f, 0x5d, 0xcf, 0x06},
		.updateChallenge = {0x42},
		.versionID = 1,
		.formatVersionCompatibility = MANIFEST_FORMAT_VERSION,
		.updateInProgress = DEFAULT_64B_FLASH_VALUE,
		.valid = VALID_64B_VALUE
};

//In between, all the update code is shoved via the HERMES_CRITICAL macro
volatile const uint8_t __attribute__((section(".rodata.Hermes.cache$1"), aligned(BLOCK_SIZE))) backupCache1[BLOCK_SIZE] = {[0 ... (BLOCK_SIZE - 1)] = 0xff};
volatile const uint8_t __attribute__((section(".rodata.Hermes.cache$3"), aligned(BLOCK_SIZE))) backupCache2[BLOCK_SIZE] = {[0 ... (BLOCK_SIZE - 1)] = 0xff};

uint8_t cacheRAM[BLOCK_SIZE] = {0};

HERMES_CRITICAL volatile const UpdateMetadata * getMetadata()
{
	if(isMetadataValid(updateMetadataMain))
		return &updateMetadataMain;

	assert(isMetadataValid(updateMetadataSec));
	return &updateMetadataSec;
}

void setMetadataPage(const UpdateMetadata * currentMetadata, const UpdateHeader * updateLocation, uint32_t multiplier);
void resetMetadataPage(const UpdateMetadata * currentMetadata, uint32_t multiplier);

//Clean all the metadata fields when starting the update so that we don't attempt to recover from a crash and load untrusted data
HERMES_CRITICAL bool startUpdate()
{
	CriticalMetadata criticalMetadataCopy = criticalMetadata;

	//This step is extremely security-critical. If the flag could be incorrectly read, Munin would incorrectly assume public pages contained trusted data
	if(criticalMetadataCopy.updateInProgress != 0)
	{
		erasePage((size_t) backupCache1);
		erasePage((size_t) backupCache2);

		//Reset the active metadata page
		resetMetadataPage((const UpdateMetadata *) getMetadata(), 0);

		//We set the updateInProgress bit
		uint64_t qword = 0;
		writeToNAND((size_t) &criticalMetadata.updateInProgress, sizeof(qword), (const uint8_t *) &qword);
	}

	return true;
}

void requestUpdate(const void * updateLocation)
{
	setMetadataPage((const UpdateMetadata *) getMetadata(), updateLocation, 0);
}

HERMES_CRITICAL void concludeUpdate(bool withError)
{
	//We backup the critical metadata page
	erasePage((size_t) backupCache1);

	CriticalMetadata criticalMetadataCopy = criticalMetadata;

	//If we're done applying the update, let's update the next update's challenge, and the versionID
	if(!withError)
	{
		const UpdateHeader * header = getMetadata()->location;

		memcpy(criticalMetadataCopy.updateChallenge, header->sectionSignedUpdateKey.signature, sizeof(criticalMetadataCopy.updateChallenge));
		criticalMetadataCopy.versionID = header->sectionSignedDeviceKey.versionID;
	}

	//Did the update even start, or was that a simple misfire?
	if(!withError || criticalMetadataCopy.updateInProgress == DEFAULT_64B_FLASH_VALUE)
	{
		//We reset the update bit
		criticalMetadataCopy.updateInProgress = DEFAULT_64B_FLASH_VALUE;

		writeToNAND((size_t) backupCache1, sizeof(CriticalMetadata), (const uint8_t *) &criticalMetadataCopy);

		//Erase the main metadata page
		erasePage((size_t) &criticalMetadata);

		//Write the page with an invalid flag
		criticalMetadataCopy.valid = DEFAULT_64B_FLASH_VALUE;
		writeToNAND((size_t) &criticalMetadata, sizeof(CriticalMetadata), (const uint8_t *) &criticalMetadataCopy);

		//Wipe the few bits so that the page is now valid
		criticalMetadataCopy.valid = VALID_64B_VALUE;
		writeToNAND((size_t) &criticalMetadata.valid, sizeof(criticalMetadataCopy.valid), (const uint8_t *) &criticalMetadataCopy.valid);
	}

	enableIRQ();
}

HERMES_CRITICAL void validateCriticalMetadata()
{
	//If we could hide a public key somewhere, it'd be nice to sign the page and thus protect against forgery and leading us to trust an incorrect updateInProgress
	if(criticalMetadata.valid == VALID_64B_VALUE)
		return;

	//Erase the main metadata page
	erasePage((size_t) &criticalMetadata);

	//Load the backup
	CriticalMetadata criticalMetadataCopy = *(const CriticalMetadata *) backupCache1;

	//Write the page with an invalid flag
	criticalMetadataCopy.valid = DEFAULT_64B_FLASH_VALUE;
	writeToNAND((size_t) &criticalMetadata, sizeof(CriticalMetadata), (const uint8_t *) &criticalMetadataCopy);

	//Wipe the few bits so that the page is now valid
	criticalMetadataCopy.valid = VALID_64B_VALUE;
	writeToNAND((size_t) &criticalMetadata.valid, sizeof(criticalMetadataCopy.valid), (const uint8_t *) &criticalMetadataCopy.valid);
}

HERMES_CRITICAL void bootloaderPerformUpdate()
{
	validateCriticalMetadata();

	disableIRQ();
	const UpdateHeader * header = getMetadata()->location;

	/*
	 * This code is the entry point of the update installer.
	 * This code is supposed to be called at every boot, and check if an update is ready to be installed
	 * First, we read the update location. If it not remotely valid, we stop immediately.
	 * Second, we look for valid cryptographic signatures, and header consistency.
	 * If both test pass, we start the update and trust the criticalMetadata page
	 */

	//Validate if we have a remotely sensible update location
	if((((uintptr_t) header) & BLOCK_OFFSET_MASK) != 0 || ((uintptr_t) header) >= FLASH_SIZE)
	{
		return enableIRQ();
	}

	/*
	 * Warning: This code isn't resilient to fault injections
	 */

	//Authenticate the header. This step is also how we detect whenever we have a valid update to install
	if(!validateHeader(header))
	{
		return enableIRQ();
	}

	//At this point the header is fully validated, we can validate the image
	if(!validateImage(header))
	{
		return enableIRQ();
	}

	//Actually start performing the update

	//Signal the update started and reset the metadata if not recovering from a power loss
	if(!startUpdate())
	{
		return concludeUpdate(true);
	}

	const uint8_t * baseCommand = &((const uint8_t *) header)[sizeof(UpdateHeader)];
	size_t index = 0, traceCounter = 0;
	const size_t permanentTraceCounter = getCurrentCounter();

	//We do a dry run to make sure everything is working properly before doing anything destructive

	//We first validate everything will properly decode
	if(!runCommands(baseCommand, &index, header->sectionSignedDeviceKey.manifestLength, &traceCounter, permanentTraceCounter, true))
	{
		return concludeUpdate(true);
	}

   //Then, we check everything will decompress successfully
	if(!applyDeltaPatch(header, index, traceCounter, permanentTraceCounter, true))
	{
		return concludeUpdate(true);
	}

	//Okay, everything should be good. We will go ahead and run the update
	index = traceCounter = 0;

	//We're not checking the return value after this point because the update has started and it isn't actionnable

	//We first run the commands
	runCommands(baseCommand, &index, header->sectionSignedDeviceKey.manifestLength, &traceCounter, permanentTraceCounter, false);

	//Perform the BSDiff it returns whether the new layout validated the checks appended to the manifest. Not sure what to do of them. Maybe restore the old image if we have one handy?
	applyDeltaPatch(header, index, traceCounter, permanentTraceCounter, false);

	//Mark the update as complete
	concludeUpdate(false);

	//Reboot
	reboot();
}

void _start(void);
HERMES_CRITICAL void _start_with_update()
{
	bootloaderPerformUpdate();
	_start();
}
