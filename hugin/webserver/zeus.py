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
