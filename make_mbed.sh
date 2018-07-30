#!/bin/sh

rm -rf ../tmp
mkdir -p ../tmp
cd ../tmp

cp -R ../project/common .
cp -R ../project/munin/Bytecode .
cp -R ../project/munin/Delta .
cp -R ../project/munin/Userland .
cp ../project/munin/*.c .
cp ../project/munin/*.h .

#Copy integration code
cp -R ../project/munin/integration/mbedOS/* .
cp ../project/munin/integration/mbedOS/.mbed .

#Copy the drivers
cp -R ../project/munin/integration/drivers/K64F/ .
mv K64F/ device

mv network/easy-connect* .
mv network/network.cpp .
mv network/network.h .
mv network/mbed_app.json .
rm -rf network

rm -rf common/lzfx-4k common/crypto/core.c common/crypto/sha256.min.c

# mbed deploy

rm -rf common/crypto/libhydrogen/tests
mbed compile -m K64F -t GCC_ARM --profile mbed-os/tools/profiles/debug.json -c
