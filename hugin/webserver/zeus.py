"""
Copyright (C) 2018 Orange

This software is distributed under the terms and conditions of the 'BSD-3-Clause-Clear'
license which can be found in the file 'LICENSE.txt' in this package distribution
or at 'https://spdx.org/licenses/BSD-3-Clause-Clear.html'.
"""

import sys

from importConfig import runImported
from server import runServer


def printHelp():
	print("Need at list one argument (server or import): \nArgument `server` start the Zeus server.")
	print("Argument `import` import a config to Zeus")


if __name__ == '__main__':
	if len(sys.argv) >= 2:
		if sys.argv[1] == "server":
			runServer()

		elif sys.argv[1] == "import":
			runImported()

		else:
			printHelp()

	else:
		printHelp()
