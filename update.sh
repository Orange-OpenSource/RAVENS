#!/bin/sh

rm -rf output final zeus/update/FactorioBot.old

cmake-build-debug/hugin/hugin diff --batchMode --config test_files/config.json -o output
cmake-build-debug/hugin/hugin authenticate -p output -o final -k test_files/priv.key
python3 zeus/zeus.py import -d FactorioBot -i final/ -o /home/taiki/uvisor_update/project/zeus/update/
