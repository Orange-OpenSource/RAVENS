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

#ifndef RAVENS_DRIVER_API_H
#define RAVENS_DRIVER_API_H

//Device specific APIs

#ifdef __cplusplus
extern "C"
{
#endif

#include "core.h"
void reboot();
void enableIRQ();
void disableIRQ();

//Those methods must always be available during the update
#ifndef NO_CRITICAL
	RAVENS_CRITICAL void eraseSector(size_t address);
	RAVENS_CRITICAL void programFlash(size_t address, const uint8_t *data, size_t length);
#endif

#ifdef __cplusplus
};
#endif

#endif //RAVENS_DRIVER_API_H
