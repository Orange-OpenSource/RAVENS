//
//	File: crypto.h
//
//	Purpose: Main header of our crypto tools
//
//	Author: Emile-Hugo Spir
//
//	Copyright: Orange
//

#ifdef __cplusplus
extern "C" {
#endif

#include "libhydrogen/hydrogen.h"
#define CONTEXT "munin"

//General utils
bool clearMemory(uint8_t * memory, size_t length);
bool loadKey(const char * filename, bool isPrivate, uint8_t * output);
void loadKeyWithDefault(const char * filename, bool isPrivate, uint8_t * output);

// Signature utils

#define PUBLIC_KEY { 0x18, 0x4f, 0x40, 0xbe, 0x9f, 0x3e, 0xc6, 0x8f,\
					 0xa1, 0xc2, 0x76, 0x04, 0xb3, 0x8c, 0x0c, 0x4a,\
					 0xc4, 0xc7, 0x8b, 0x29, 0xc4, 0xf5, 0x05, 0xc8,\
					 0xc3, 0x58, 0x7d, 0x1f, 0x97, 0xe7, 0x59, 0x17}

#define SECRET_KEY { 0xc5, 0x49, 0x55, 0xa2, 0xfd, 0x79, 0x7d, 0x17,\
					 0xb7, 0x93, 0x51, 0xf5, 0xe1, 0x60, 0x3a, 0xeb,\
					 0x2c, 0x24, 0x9a, 0xb2, 0x6e, 0xa7, 0xda, 0x97,\
					 0x9d, 0xf9, 0x0f, 0x18, 0x38, 0xd2, 0xd1, 0x9a,\
					 0x18, 0x4f, 0x40, 0xbe, 0x9f, 0x3e, 0xc6, 0x8f,\
					 0xa1, 0xc2, 0x76, 0x04, 0xb3, 0x8c, 0x0c, 0x4a,\
					 0xc4, 0xc7, 0x8b, 0x29, 0xc4, 0xf5, 0x05, 0xc8,\
					 0xc3, 0x58, 0x7d, 0x1f, 0x97, 0xe7, 0x59, 0x17}

#define SIGNATURE_LENGTH hydro_sign_BYTES
#define PUBLIC_KEY_LENGTH hydro_sign_PUBLICKEYBYTES

bool validateSignature(const uint8_t * message, const size_t messageLength, const uint8_t signature[hydro_sign_BYTES], const uint8_t publicKey[hydro_sign_PUBLICKEYBYTES]);
bool validateSignature(const uint8_t *message, const size_t messageLength, const uint8_t *signature, const uint8_t *publicKey);
bool signBuffer(const uint8_t * message, const size_t messageLength, uint8_t signature[hydro_sign_BYTES], const uint8_t secretKey[hydro_sign_SECRETKEYBYTES]);

void generateKeyMemory(uint8_t * secretKey, uint8_t * publicKey);
bool writeKey(uint8_t * key, bool isPrivate, const char * path);

// Hashing utils

#define HASH_LENGTH 32u	//Output length of SHA-256

void hashBlock(const uint8_t * data, size_t length, uint16_t counter, bool reuseHash, uint8_t * hashBuffer);
void hashMemory(const uint8_t * data, const size_t length, uint8_t * hashBuffer);
bool hashFile(const char * filename, uint8_t * hashBuffer, size_t skip);

// High level testing functions

bool generateKeys(const char * privKeyFile, const char * pubKeyFile);
void testSignature(const uint8_t * message, bool wantFail, const char * privKeyFile, const char * pubKeyFile);
void testCrypto();
bool signFile(const char *inputFile, const char *outputFile, const char *privKeyFile);
bool signString(const char * inputString, const char * privKeyFile, uint8_t * signature);
bool verifyFile(const char *inputFile, const char * pubKeyFile);
bool verifyString(const uint8_t *inputString, size_t stringLength, const char *signature, bool isHex, const char * pubKeyFile);

#ifdef __cplusplus
}
#endif
