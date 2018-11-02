#!/bin/sh

rm -rf output final hugin/webserver/update/FactorioBot.old

hugin/Hugin diff --batchMode --config test_files/config.json -o output
# Don't. Authentificate. On. The. Same. Server.
hugin/Hugin authenticate -p output -o final -k test_files/priv.key
python3 hugin/webserver/odin.py import -d FactorioBot -i final/ -o hugin/webserver/update/
