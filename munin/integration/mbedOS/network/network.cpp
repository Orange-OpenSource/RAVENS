/*
 * Copyright (C) 2018 Orange
 *
 * This software is distributed under the terms and conditions of the 'BSD-3-Clause-Clear'
 * license which can be found in the file 'LICENSE.txt' in this package distribution
 * or at 'https://spdx.org/licenses/BSD-3-Clause-Clear.html'.
 */

/**
 * Purpose: Main networking wrapper, provide the data stream in 4kB chunks
 * @author Emile-Hugo Spir
 */

#ifdef TARGET_LIKE_MBED
	#include "TCPSocket.h"
	#include "easy-connect.h"

	Mutex networkMutex;
	Mutex dataMutex;
	TCPSocket socket;
#else
	#include <cstdint>
	#include <cstdlib>
	#include <cstdio>
#endif

#include "network.h"

#ifdef UVISOR_FEATURE

	#define UVISOR_STATIC static

#else

#define UVISOR_STATIC extern "C"
int uvisor_box_id_self()
{
	return 0;
}

#endif

#define BUFFER_LENGTH				4096

#define NEXT_BUFFER_WRITE_MASK 	(0x1u << 0u)
#define NEXT_BUFFER_READ_MASK	(0x1u << 1u)

#define BUFFER_0_READY 			(0x1u << 2u)
#define BUFFER_0_BEING_CONSUMED	(0x1u << 3u)
#define BUFFER_1_READY 			(0x1u << 4u)
#define BUFFER_1_BEING_CONSUMED	(0x1u << 5u)

#define BUFFER_READY_MASK		(BUFFER_0_READY | BUFFER_1_READY)
#define BUFFER_CONSUMPTION_MASK	(BUFFER_0_BEING_CONSUMED | BUFFER_1_BEING_CONSUMED)
#define BUFFER_0_BUSY_MASK 		(BUFFER_0_READY | BUFFER_0_BEING_CONSUMED)
#define BUFFER_1_BUSY_MASK 		(BUFFER_1_READY | BUFFER_1_BEING_CONSUMED)

static struct NetworkStreaming
{
#ifdef TARGET_LIKE_MBED
	NetworkInterface * interface;
	Thread *downloadThread;
#endif
	NetworkStreamingData transferData;
	int boxID;

	uint8_t * buffer0;
	uint8_t * buffer1;

	uint16_t buffer0Length;
	uint16_t buffer1Length;

	//	Bit layout of bufferMeta
	//	.--------------------------------------------------------------------------------------------------------------------------------------------------------.
	//	|	7	|	6	|			 5				|		  4		   |		    5			   |		 4		  |		    1		   |		  0		     |
	//	|-------|-------|---------------------------|------------------|---------------------------|------------------|--------------------|---------------------|
	//	|  N/A	|  N/A	|  Buffer 1 Being Consumed  |  Buffer 1 ready  |  Buffer 0 Being Consumed  |  Buffer 0 ready  |  Next Read Buffer  |  Next Write Buffer  |
	//	'--------------------------------------------------------------------------------------------------------------------------------------------------------'
	//	Would it be worth merging in our two bools in bit 6 and bit 7?

	//	Current buffer lifecycle:
	//		Pointed by Next Write Buffer
	//		Marked as Ready
	//		Pointed by Next Read Buffer
	//		Pointed by Buffer Being Consumed

	uint8_t bufferMeta;
	bool isInitialized;
	bool downloadIsOver;

	int callerID;
	bool shutDown;
} networkStaticMemory;

// Basic utils

static uint8_t pickBufferWrite(const uint8_t & metadata)
{

	if(metadata & NEXT_BUFFER_WRITE_MASK)
	{
		if((metadata & BUFFER_1_BUSY_MASK) == 0)
			return 1;
	}
	else
	{
		if((metadata & BUFFER_0_BUSY_MASK) == 0)
			return 0;
	}

	return 0xff;
}

static uint8_t pickBufferRead(const uint8_t & metadata)
{
	if(metadata & NEXT_BUFFER_READ_MASK)
	{
		if(metadata & BUFFER_1_READY)
			return 1;
	}
	else
	{
		if(metadata & BUFFER_0_READY)
			return 0;
	}

	return 0xff;
}

static inline bool haveBufferReady(const uint8_t & metadata)
{
	return (metadata & BUFFER_READY_MASK) != 0;
}

//Setting up the network stack

static bool NetworkStreamingStart(struct NetworkStreaming & data)
{
	if(!data.isInitialized)
	{
		networkStaticMemory.shutDown = false;

#ifdef TARGET_LIKE_MBED
		data.interface = easy_connect(false);

		if(data.interface != NULL)
		{
			data.isInitialized = true;
		}
#endif
	}

	return data.isInitialized;
}

void proxyReleaseData();

static void NetworkStreamingShutdown(struct NetworkStreaming & data)
{
	proxyReleaseData();
#ifdef TARGET_LIKE_MBED
	networkMutex.lock();

	data.interface->disconnect();
	data.isInitialized = false;
#endif

	free(data.buffer0);
	free(data.buffer1);

	data.buffer0 = NULL;
	data.buffer1 = NULL;

#ifdef TARGET_LIKE_MBED
	if(data.downloadThread->get_state() != Thread::Inactive)
		data.downloadThread->join();
	delete data.downloadThread;
	data.downloadThread = NULL;

	networkMutex.unlock();
#endif
}

//Create a new request

static void downloadDataProxy();

static bool newRequest(struct NetworkStreaming & data, const char * domain, const uint port, const char * request, const uint requestLength, int _boxID)
{
	if(domain == NULL || port == 0 || request == NULL || requestLength == 0)
		return false;

	if(!NetworkStreamingStart(data))
		return false;

#ifdef TARGET_LIKE_MBED
	if(!networkMutex.trylock())
		return false;

	if(data.downloadThread != NULL)
	{
		if(data.downloadThread->get_state() == Thread::Running)
			data.downloadThread->join();

		delete data.downloadThread;
		data.downloadThread = NULL;
	}

	data.downloadThread = new Thread();
	if(data.downloadThread == NULL)
		return false;

#endif

	if(data.buffer0 == NULL)
	{
		data.buffer0 = (uint8_t *) malloc(BUFFER_LENGTH);
		if(data.buffer0 == NULL)
		{
#ifdef TARGET_LIKE_MBED
			networkMutex.unlock();
#endif
			return false;
		}
	}

	if(data.buffer1 == NULL)
	{
		data.buffer1 = (uint8_t *) malloc(BUFFER_LENGTH);
		if(data.buffer1 == NULL)
		{
			free(data.buffer0);
			data.buffer0 = NULL;
#ifdef TARGET_LIKE_MBED
			networkMutex.unlock();
#endif
			return false;
		}
	}

#ifdef TARGET_LIKE_MBED
	dataMutex.lock();

	socket.open(data.interface);

	if(socket.connect(domain, port) != 0)
	{
		dataMutex.unlock();
		proxyCloseSession();
		return false;
	}

	socket.send(request, requestLength);
#endif

	data.boxID = _boxID;
	data.downloadIsOver = false;
	data.bufferMeta = 0;

#ifdef TARGET_LIKE_MBED
	dataMutex.unlock();

	data.downloadThread->start(&downloadDataProxy);
#endif

	return true;
}

//Perform the downloading

static void downloadData(NetworkStreaming & data)
{
	while(!data.downloadIsOver)
	{
#ifdef TARGET_LIKE_MBED
		dataMutex.lock();
		const uint8_t bufferToPick = pickBufferWrite(data.bufferMeta);
		dataMutex.unlock();

		//We have to fill a buffer
		if(bufferToPick != 0xff)
		{
			//Download data into the appropriate buffer
			uint8_t * bufferToWrite = bufferToPick ? data.buffer1 : data.buffer0;
			nsapi_size_or_error_t output = socket.recv(bufferToWrite, BUFFER_LENGTH);
			if(output < 0)
			{
				printf("Error (%d) !\n", output);
			}
			else if(output == 0)
			{
				data.downloadIsOver = true;
			}
			else if (output <= BUFFER_LENGTH)
			{
				//We (at least partially) filled the buffer
				dataMutex.lock();

				//Mark the buffer as ready
				data.bufferMeta |= bufferToPick ? BUFFER_1_READY : BUFFER_0_READY;

				//Flip the NEXT_BUFFER_WRITE bit
				data.bufferMeta ^= NEXT_BUFFER_WRITE_MASK;

				if(bufferToPick)
					data.buffer1Length = output;
				else
					data.buffer0Length = output;

				if(output < BUFFER_LENGTH)
				{
					//We fill the end of the buffer with 0 in order not to leak data
					memset(&bufferToWrite[output], 0, BUFFER_LENGTH - output);
				}

				dataMutex.unlock();
			}
		}
		else
		{
			Thread::wait(50);
		}
#endif
	}

#ifdef TARGET_LIKE_MBED
	data.downloadThread->terminate();
#endif
}

static void downloadDataProxy()
{
	downloadData(networkStaticMemory);
}

//Give some data to the requester

static uint8_t * consumeData(uint32_t & length)
{
	if(networkStaticMemory.boxID == -1 || networkStaticMemory.boxID != networkStaticMemory.callerID)
		return NULL;

#ifdef TARGET_LIKE_MBED
	dataMutex.lock();
#endif

	uint8_t & state = networkStaticMemory.bufferMeta;

	uint8_t * output;

	//First, we clear the Being Consumed flag of the other buffer so we can start downloading in it
	//If the next buffer to read is ready, we put it in output
	//We also clear the BUFFER_X_READY bit, and mark the buffer as being consumed
	uint8_t bufferToRead = pickBufferRead(state);
	if(bufferToRead)
	{
		if(state & BUFFER_0_BEING_CONSUMED)
			state &= ~BUFFER_0_BEING_CONSUMED;

		if(state & BUFFER_1_READY)
		{
			output = networkStaticMemory.buffer1;
			state &= ~BUFFER_1_READY;
			state |= BUFFER_1_BEING_CONSUMED;
		}
		else
			output = NULL;
	}
	else
	{
		if(state & BUFFER_1_BEING_CONSUMED)
			state &= ~BUFFER_1_BEING_CONSUMED;

		if(state & BUFFER_0_READY)
		{
			output = networkStaticMemory.buffer0;
			state &= ~BUFFER_0_READY;
			state |= BUFFER_0_BEING_CONSUMED;
		}
		else
			output = NULL;
	}

	if(output != NULL)
	{
		//Flip NEXT_BUFFER_READ_MASK
		state ^= NEXT_BUFFER_READ_MASK;

		//Grab the length of the buffer
		length = output == networkStaticMemory.buffer0 ? networkStaticMemory.buffer0Length : networkStaticMemory.buffer1Length;
	}

#ifdef TARGET_LIKE_MBED
	dataMutex.unlock();
#endif

	return output;
}

//Functions called from RPC

UVISOR_STATIC bool proxyNewRequest(const char * domain, const uint16_t port, const char * request, const uint32_t requestLength)
{
	//Can only be called from the main thread
	if(uvisor_box_id_self() != 0)
		return false;

	return newRequest(networkStaticMemory, domain, port, request, requestLength, networkStaticMemory.callerID);
}

UVISOR_STATIC uint8_t proxyRequestHasData()
{
	if(!networkStaticMemory.isInitialized || uvisor_box_id_self() != 0)
		return 0;

#ifdef TARGET_LIKE_MBED
	dataMutex.lock();
#endif
	const bool status = haveBufferReady(networkStaticMemory.bufferMeta);

	//Nothing to read and no new data coming?
	const bool isFinished = status == false && networkStaticMemory.downloadIsOver;
#ifdef TARGET_LIKE_MBED
	dataMutex.unlock();
#endif

	if(isFinished)
		return NETWORK_FINISHED;

	else if(status == false)
		return NETWORK_NO_DATA;

	return NETWORK_HAS_DATA;
}

UVISOR_STATIC const NetworkStreamingData * proxyRequestGetData()
{
	if(!networkStaticMemory.isInitialized || uvisor_box_id_self() != 0)
		return 0;

	uint32_t length;
	uint8_t * buffer = consumeData(length);

	networkStaticMemory.transferData.buffer = buffer;
	networkStaticMemory.transferData.length = length;

	return &networkStaticMemory.transferData;
}

UVISOR_STATIC void proxyReleaseData()
{
	if(networkStaticMemory.isInitialized && uvisor_box_id_self() == 0)
	{
#ifdef TARGET_LIKE_MBED
		dataMutex.lock();
#endif

		uint8_t & status = networkStaticMemory.bufferMeta;

		//Mark all remaining buffers being consumed as done
		status &= ~BUFFER_CONSUMPTION_MASK;

		//If the download is over and
		if(networkStaticMemory.downloadIsOver && haveBufferReady(status) == false)
		{
#ifdef TARGET_LIKE_MBED
			if(socket.close() == 0)
				networkMutex.unlock();
#endif
		}

#ifdef TARGET_LIKE_MBED
		dataMutex.unlock();
#endif
	}
}

UVISOR_STATIC void proxyCloseSession()
{
	if(networkStaticMemory.isInitialized && uvisor_box_id_self() == 0)
	{
#ifdef TARGET_LIKE_MBED
		dataMutex.lock();

		if(!networkStaticMemory.downloadIsOver && socket.close() == 0)
			networkMutex.unlock();

		dataMutex.unlock();
#endif

		proxyReleaseData();
	}
}

UVISOR_STATIC void proxyShutDown()
{
	if(networkStaticMemory.isInitialized)
	{
#ifdef UVISOR_FEATURE
		networkStaticMemory.shutDown = true;
#else
		NetworkStreamingShutdown(networkStaticMemory);
#endif
	}

}

//Main RPC receiver loop

#ifdef UVISOR_FEATURE
void networkingRPC()
{
	// List of functions to wait for
	static const TFN_Ptr functions[] = {
		(TFN_Ptr) proxyNewRequest,
		(TFN_Ptr) proxyRequestHasData,
		(TFN_Ptr) proxyRequestGetData,
		(TFN_Ptr) proxyReleaseData,
		(TFN_Ptr) proxyShutDown
	};

	NetworkStreamingStart(networkStaticMemory);

	while(!networkStaticMemory.shutDown)
	{
		int status = rpc_fncall_waitfor(functions, 5, &networkStaticMemory.callerID, UVISOR_WAIT_FOREVER);

		if (status)
		{
			uvisor_error(USER_NOT_ALLOWED);
		}
	}

	NetworkStreamingShutdown(networkStaticMemory);
}
#endif
