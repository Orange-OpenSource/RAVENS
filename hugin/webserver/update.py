"""
Copyright (C) 2018 Orange

This software is distributed under the terms and conditions of the 'BSD-3-Clause-Clear'
license which can be found in the file 'LICENSE.txt' in this package distribution
or at 'https://spdx.org/licenses/BSD-3-Clause-Clear.html'.
"""

import base64
import binascii
import subprocess


def sendFileContent(handler, filename):
	try:
		with open(filename, "rb") as file:

			handler.send_response(200)
			handler.end_headers()

			data = file.read(1024)
			while data:
				handler.wfile.write(data)
				data = file.read(1024)
	except IOError:
		handler.invalidRequest()


def updateRequest(deviceData, ver):
	if int(ver) >= int(deviceData['currentVersion']):
		return False

	# Only return true if we actually have a payload to send
	return ver in deviceData['payload']


def getDeltaUpdateFile(deviceData, ver):
	return deviceData['payload'][ver]['manifest1']


def getMainPayloadUpdateFile(deviceData, ver):
	return deviceData['payload'][ver]['manifest2']


def sendInterlacedReply(handler, deviceData, ver, signedChallenge):
	updateFilename = getDeltaUpdateFile(deviceData, ver)

	# If we have a verificationIndex field, we need to split the manifest 1 at a given index
	if 'verificationIndex' in deviceData['payload'][ver]:
		try:
			with open(updateFilename, "rb") as file:
				handler.send_response(200)
				handler.end_headers()

				# Read the first segment of the file, send it away, then append the challenge
				data = file.read(deviceData['payload'][ver]['verificationIndex'])
				handler.wfile.write(data)
				handler.wfile.write(signedChallenge)

				# Send the end of the file
				data = file.read(1024)
				while data:
					handler.wfile.write(data)
					data = file.read(1024)

		except IOError:
			return handler.invalidRequest()

	else:
		sendFileContent(handler, updateFilename)
		handler.wfile.write(signedChallenge)


def replyChallenge(deviceData, ver, challenge):
	# Add the challenge to the array we will get the tool to authenticate to answer the challenge
	rawChallenge = bytearray(base64.decodebytes(challenge.encode())[:64])

	if len(rawChallenge) != 64:
		return False, ""

	# Add the version
	rawChallenge += int(ver).to_bytes(4, 'little')
	rawChallenge += int(deviceData['currentVersion']).to_bytes(4, 'little')

	rawChallenge += binascii.unhexlify(deviceData['payload'][ver]['publicKey'])

	# Get the random
	try:
		with open("/dev/urandom", "rb") as randomFile:
			random = bytearray(randomFile.read(8))
			rawChallenge += random

	except IOError:
		return False, ""

	process = subprocess.Popen((deviceData['sign_util'], 'crypto', '--signString', rawChallenge.hex(), deviceData['payload'][ver]['privateKey']), stdout=subprocess.PIPE)
	process.wait()
	outputRaw = process.stdout.read().decode("utf-8").split(' ')

	if outputRaw[0] != 'Signature:':
		return False

	signedChallenge = binascii.unhexlify(outputRaw[1])
	signedChallenge += random

	return True, signedChallenge
