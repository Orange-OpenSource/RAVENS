//
// Created by Emile-Hugo Spir on 6/29/18.
//

#ifndef HERMES_DRIVER_API_H
#define HERMES_DRIVER_API_H

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
	HERMES_CRITICAL void eraseSector(size_t address);
	HERMES_CRITICAL void programFlash(size_t address, const uint8_t *data, size_t length);
#endif

#ifdef __cplusplus
};
#endif

#endif //HERMES_DRIVER_API_H
