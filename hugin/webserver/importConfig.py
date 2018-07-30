import argparse
import json
import os
import shutil
import sys


def getAuthorization(default="yes"):
	valid = {"yes": True, "y": True, "ye": True, "no": False, "n": False}
	if default == "yes":
		prompt = " [Y/n] "
		defaultRet = True
	elif default == "no":
		prompt = " [y/N] "
		defaultRet = False
	else:
		prompt = " [y/n] "
		defaultRet = None

	while True:
		sys.stdout.write(prompt)
		choice = input().lower()
		if defaultRet is not None and choice == '':
			return defaultRet
		elif choice in valid:
			return valid[choice]
		else:
			sys.stdout.write("Please respond with 'yes' or 'no' (or 'y' or 'n').\n")


def runImported():
	parser = argparse.ArgumentParser(
		description='Import the update files to a new firmware version into a Zeus server.')
	parser.add_argument('-d', '--deviceName', metavar='device', required=True, help='the name of the device to import')
	parser.add_argument('-i', '--importPath', metavar='importPath', required=True,
						help='path to the directory containing the update.json to import')
	parser.add_argument('-o', '--output', metavar='server', default='.',
						help='path to the Zeus "update" directory (-o /path/to/zeus/update/)')
	parser.add_argument('-s', '--signUtil', metavar='sign',
						help='the signing util Zeus should use to sign its answers to devices')
	args, _ = parser.parse_known_args(sys.argv)

	if len(args.output) == 0 or len(args.deviceName) == 0:
		print('Error, invalid arguments!')
		return

	try:
		with open(args.importPath + '/update.json', 'r') as refFile:
			toImport = json.load(refFile)

	except (IOError, json.JSONDecodeError):
		print("Couldn't open the update.json to import at path " + args.importPath + '/update.json')
		return

	try:
		with open(args.output + '/update.json', 'r') as refFile:
			output = json.load(refFile)

	except (IOError, json.JSONDecodeError):
		print("Couldn't open the update.json to write into at path " + args.output + '/update.json. Create?')
		if not getAuthorization():
			return

		# Create the path
		if not os.path.exists(args.output):
			os.makedirs(args.output)

		output = []

	if args.deviceName not in output:
		if "sign" not in args:
			print("The --signUtil flag is mandatory when the device isn't already in Zeus' database")
			return

		entry = {'sign_util': args.sign}
	else:
		entry = output[args.deviceName]
		del entry['payload']

	if "currentVersion" not in toImport or "payload" not in toImport:
		print('Importing invalid format, stopping!')
		return

	entry['currentVersion'] = toImport["currentVersion"]

	# Get an empty directory
	oldDirectoryPath = args.output + '/' + args.deviceName + ".old"
	if os.path.exists(oldDirectoryPath):
		print('Existing archive at ' + oldDirectoryPath + '.\nWe need to delete it to proceed. Confirm?')
		if not getAuthorization():
			return

		shutil.rmtree(oldDirectoryPath)

	outputPath = args.output + '/' + args.deviceName + '/'
	if os.path.exists(outputPath):
		print('Renaming ' + outputPath + ' to ' + oldDirectoryPath)
		os.rename(outputPath, oldDirectoryPath)

	os.makedirs(outputPath)

	payloadArray = {}
	importPayload = toImport['payload']

	# Start parsing and copying the input
	for key in importPayload:
		if int(key) >= int(toImport['currentVersion']):
			print(
				'Version ' + key + ' is larger than the active version (' + toImport['currentVersion'] + '). Aborting')
			shutil.rmtree(outputPath)
			os.rename(oldDirectoryPath, outputPath)
			return

		shutil.copyfile(args.importPath + '/' + importPayload[key]['manifest1'], outputPath + 'manifest1_' + key)
		shutil.copyfile(args.importPath + '/' + importPayload[key]['manifest2'], outputPath + 'manifest2_' + key)

		try:
			with open(outputPath + '/priv_' + key, 'w+') as refFile:
				refFile.write(importPayload[key]['privateKey'])
		except IOError:
			print("Couldn't write the private key for version " + key + "!")
			shutil.rmtree(outputPath)
			os.rename(oldDirectoryPath, outputPath)
			return

		newVersion = {'publicKey': importPayload[key]['publicKey'],
		              'privateKey': outputPath + '/priv_' + key,
		              'manifest1': outputPath + 'manifest1_' + key,
		              'manifest2': outputPath + 'manifest2_' + key}

		if 'validation' in importPayload[key]:
			if 'validationIndex' not in importPayload[key]:
				print("Invalid config file: given ranges to check but not index in payload!")
				shutil.rmtree(outputPath)
				os.rename(oldDirectoryPath, outputPath)
				return

			newVersion['verification'] = importPayload[key]['validation']
			newVersion['verificationIndex'] = importPayload[key]['validationIndex']

		payloadArray[key] = newVersion

	entry['payload'] = payloadArray
	output[args.deviceName] = entry

	try:
		shutil.copyfile(args.output + '/update.json', args.output + '/update.json.old')
		with open(args.output + '/update.json', 'w+') as refFile:
			json.dump(output, refFile, indent=4, separators=(',', ': '))
		os.remove(args.output + '/update.json.old')

	except (IOError, json.JSONDecodeError):
		print("Couldn't write the update json! Aborting.")
		shutil.rmtree(outputPath)
		os.rename(oldDirectoryPath, outputPath)

		# Restore the old update manifest
		os.remove(args.output + '/update.json')
		shutil.copyfile(args.output + '/update.json.old', args.output + '/update.json')
		return

	print(
		'Successfully imported update for version ' + str(toImport['currentVersion']) + ' of ' + args.deviceName + '!')
