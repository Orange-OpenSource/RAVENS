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

#ifndef HERMES_DEVICE_CONFIG_H
#define HERMES_DEVICE_CONFIG_H

//Device config

#define DEVICE_NAME "FactorioBot"
#define UPDATE_SERVER_NAME "Odin"
#define UPDATE_SERVER "172.16.0.85"
#define UPDATE_SERVER_PORT 8080

//Device drivers

//ADDRESSING_GRANULARITY is the smallest unit we're willing to pad. If set to 1, we accept padding anything. Don't set to 0
#define ADDRESSING_GRANULARITY (1u << 2u)

//Smallest supported write to NAND in bytes
#define WRITE_GRANULARITY (1u << 3u)

//Smallest supported write to NAND in bits. Default to 8 * WRITE_GRANULARITY
//#define MIN_BIT_WRITE_SIZE	64u

//How many bits are needed to encode the length of the flash?
#define FLASH_SIZE_BIT	20u

//How many bits are needed to encode the length of a block of NAND flash
#define BLOCK_SIZE_BIT	12u	// 4096

//Update requests

#define BASE_REQUEST "GET /manifest HTTP/1.1\r\nHost: " UPDATE_SERVER ":" STR(UPDATE_SERVER_PORT) "\r\nUser-Agent: " DEVICE_NAME "/"
#define SEC_REQUEST "\r\nX-Update-Challenge: "
#define FINAL_REQUEST "\r\n\r\n"

#define PAYL_REQUEST "POST /payload HTTP/1.1\r\nHost: " UPDATE_SERVER ":" STR(UPDATE_SERVER_PORT) "\r\nUser-Agent: " DEVICE_NAME "/"
#define PAYL_SEC_REQUEST "\r\nContent-Length: "

#define EXPECTED_REPLY_UPDATE "HTTP/1.0 200 OK\r\nServer: " UPDATE_SERVER_NAME "\r\nDate: \r\n\r\n"
#define EXPECTED_REPLY_NO_UPDATE "HTTP/1.0 204 No Content\r\nServer: " UPDATE_SERVER_NAME "\r\nDate: \r\n\r\n"

#endif //HERMES_DEVICE_CONFIG_H
