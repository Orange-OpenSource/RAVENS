//
// Created by Emile-Hugo Spir on 5/2/18.
//

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
