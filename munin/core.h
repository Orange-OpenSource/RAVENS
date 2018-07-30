//
// Created by Emile-Hugo Spir on 4/26/18.
//

#ifndef HERMES_CORE_H
#define HERMES_CORE_H

#include "io_management.h"
#include "../common/layout.h"

#define HERMES_CRITICAL __attribute__((section(".rodata.Hermes.cache$2")))

#define isMetadataValid(a) ((a).footer.valid == VALID_64B_VALUE && (a).footer.notExpired == DEFAULT_64B_FLASH_VALUE)

volatile const UpdateMetadata * getMetadata();
void requestUpdate(const void * updateLocation);

#endif //HERMES_CORE_H
