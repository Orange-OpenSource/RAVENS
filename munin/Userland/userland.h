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

#ifndef RAVENS_USERLAND_H
#define RAVENS_USERLAND_H

#ifndef TARGET_LIKE_MBED
	void itoa(uint32_t value, char * string, int base);
#endif
uint32_t base64_encode(const unsigned char *data, uint32_t input_length, char * output);
void eraseNecessarySpace(const void * basePointer, uint32_t length);

uint32_t getVersion();
void checkUpdate();

#endif //RAVENS_USERLAND_H
