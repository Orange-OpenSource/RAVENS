//
// Created by Emile-Hugo Spir on 5/2/18.
//

#ifndef HERMES_USERLAND_H
#define HERMES_USERLAND_H

#ifndef TARGET_LIKE_MBED
	void itoa(uint32_t value, char * string, int base);
#endif
uint32_t base64_encode(const unsigned char *data, uint32_t input_length, char * output);
void eraseNecessarySpace(const void * basePointer, uint32_t length);

uint32_t getVersion();
void checkUpdate();

#endif //HERMES_USERLAND_H
