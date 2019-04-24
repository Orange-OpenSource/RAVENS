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

#ifndef RAVENS_EXECUTION_H
#define RAVENS_EXECUTION_H

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

#endif //RAVENS_EXECUTION_H
