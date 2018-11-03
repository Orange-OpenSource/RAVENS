"""
Copyright (C) 2018 Orange

This software is distributed under the terms and conditions of the 'BSD-3-Clause-Clear'
license which can be found in the file 'LICENSE.txt' in this package distribution
or at 'https://spdx.org/licenses/BSD-3-Clause-Clear.html'.
"""

import binascii
import hmac
import time
import json
from http.server import HTTPServer, BaseHTTPRequestHandler

from update import updateRequest, replyChallenge, getMainPayloadUpdateFile, sendInterlacedReply, sendFileContent

PORT_NUMBER = 8080

class Odin(BaseHTTPRequestHandler):

	def version_string(self):
		return "Odin"

	def date_time_string(self, timestamp=None):
		return ""

	"""Respond to a GET request."""

	def invalidRequest(self):
		self.send_response(403)
		self.end_headers()

	def processManifest(self):
		userAgent, version = self.headers.get('User-Agent').split('/', 1)

		try:
			with open('update/update.json', 'r') as refFile:
				refData = json.load(refFile)
				device = refData[userAgent]
		except (IOError, json.JSONDecodeError):
			return self.invalidRequest()

		"""When the device that requested an update is up-to-date"""
		if not updateRequest(device, version):
			self.send_response(204)
			self.end_headers()
			return

		"""We compute the challenge response"""
		status, signedChallenge = replyChallenge(device, version, self.headers.get('X-Update-Challenge'))
		if not status:
			return self.invalidRequest()

		"""We need to update the device"""
		sendInterlacedReply(self, device, version, signedChallenge)

	def do_GET(self):

		if self.path == '/manifest':
			self.processManifest()

		else:
			self.send_response(302)
			self.send_header('Location', 'http://www.nyan.cat/original')
			self.end_headers()

	def processPayload(self):
		userAgent, version = self.headers.get('User-Agent').split('/', 1)

		try:
			with open('update/update.json', 'r') as refFile:
				refData = json.load(refFile)
				device = refData[userAgent]
		except (IOError, json.JSONDecodeError):
			return self.invalidRequest()

		"""When the device that requested an update is up-to-date"""
		if not updateRequest(device, version):
			self.send_response(204)
			self.end_headers()
			return

		"""We validate that the object is in the expected state"""
		if 'verification' in device['payload'][version]:
			"""Each hash in our json file is hex encoded. The POST data are raw"""

			for hash in device['payload'][version]['verification']:
				requestHash = self.rfile.read(int(len(hash) / 2))

				# Missing hash or incomplete
				if not requestHash or len(requestHash) != len(hash) / 2:
					return self.invalidRequest()

				# Check the hashes. To prevent potential guessing of the expected hashes, we use a constant time compare
				if not hmac.compare_digest(requestHash, binascii.unhexlify(hash)):
					return self.invalidRequest()

			"""At this point, we passed the check"""

		"""We need to update the device"""
		sendFileContent(self, getMainPayloadUpdateFile(device, version))

	def do_POST(self):

		if self.path == '/payload':
			self.processPayload()

		else:
			self.send_response(302)
			self.send_header('Location', 'http://www.nyan.cat/original')
			self.end_headers()


def runServer():
	server_class = HTTPServer
	httpd = server_class(('0.0.0.0', PORT_NUMBER), Odin)
	print(time.asctime(), "Server Starts - %d" % PORT_NUMBER)

	try:
		httpd.serve_forever()
	except KeyboardInterrupt:
		pass

	httpd.server_close()
	print(time.asctime(), "Server Stops - %d" % PORT_NUMBER)
