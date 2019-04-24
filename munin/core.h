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

#ifndef RAVENS_CORE_H
#define RAVENS_CORE_H

#include "io_management.h"
#include "../common/layout.h"

#define RAVENS_CRITICAL __attribute__((section(".rodata.Ravens.cache$2")))

#define isMetadataValid(a) ((a).footer.valid == VALID_64B_VALUE && (a).footer.notExpired == DEFAULT_64B_FLASH_VALUE)

volatile const UpdateMetadata * getMetadata();
void requestUpdate(const void * updateLocation);

#endif //RAVENS_CORE_H
