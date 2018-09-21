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

#ifndef HERMES_NETWORK_H
#define HERMES_NETWORK_H

typedef struct
{
	uint8_t * buffer;
	uint32_t length;
} NetworkStreamingData;

#ifdef __cplusplus
extern "C" {
#endif

bool proxyNewRequest(const char *domain, const uint16_t port, const char *request, const uint32_t requestLength);
uint8_t proxyRequestHasData();
const NetworkStreamingData *proxyRequestGetData();
void proxyReleaseData();
void proxyShutDown();
void proxyCloseSession();

#ifdef __cplusplus
}
#endif

enum
{
	NETWORK_FINISHED,
	NETWORK_NO_DATA,
	NETWORK_HAS_DATA
};

#endif //HERMES_NETWORK_H
