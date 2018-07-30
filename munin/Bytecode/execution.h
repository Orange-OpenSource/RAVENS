//
// Created by Emile-Hugo Spir on 4/26/18.
//

#ifndef HERMES_EXECUTION_H
#define HERMES_EXECUTION_H

#include "../io_management.h"

#ifdef TARGET_LIKE_MBED
	#include "../common/decoding/decoder.h"
#else
	#include <decoding/decoder.h>
#endif

bool runCommands(const uint8_t * bytes, size_t * currentByteOffset, size_t length, size_t *currentTrace, size_t oldCounter, bool dryRun);

void backupCache(size_t counter);
void restoreCache(size_t counter);

void incrementCounter(size_t *counter, size_t oldCounter, bool *fastForward);
size_t getCurrentCounter();

#endif //HERMES_EXECUTION_H
