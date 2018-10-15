# Hugin [![Build Status](https://travis-ci.org/Orange-OpenSource/RAVENS.svg?branch=master)](https://travis-ci.org/Orange-OpenSource/RAVENS)

Hugin is a command line util able to generate small delta software updates, easy to install in-place on constrained devices. Hugin can then be used to authenticate the update package, can distribute it to devices. Munin offer a reference implementation of the code necessary on the device to install the update package, in place, in a resilient manner.

Properly used, Hugin and Munin enable secure and efficient firmware updates of very small IoT devices.

# Build

## Hugin

- Run cmake to create Makefiles: `cmake .`
- Make: `make Hugin`
- Hugin is now built in `hugin/`

# How to use

## Generate an update

Hugin can generate update packages through two ways.

- The first generate a single update, using the following command: `path/to/Hugin diff -v1 path/to/old/firmware/image -v2 path/to/new/firmware/image -o path/to/output/directory/`

- The second generate update packages for many firmware images. This approach require a config file, such as the sample in `test_files`. This mode is used with the following command: `path/to/Hugin diff --batchMode --config test_files/config.json -o path/to/output/directory/`

## Sign an update

This step require access to the device master key. This cryptographic key is EXTREMELY powerful and thus should be stored on a secure computer, hopefully an HSM. At the very least, it is strongly recommended to perform the signing on a dedicated, air-gapped server.
Assuming this is the case, this is done, the following command will make Hugin sign the various update packages: `path/to/Hugin authenticate -p path/to/update/directory/ -o path/to/signed/output/directory/ -k path/to/priv.key`

## Import an update to the serveur

A small Python server implement Munin's protocol and provide various security guarantees.
Importing the updates to the server is done with the following command: `python3 hugin/webserver/zeus.py import -d <Device Name> -i path/to/signed/output/directory/ -o /path/to/server/update/storage/`

## Launching the server

Launching the small Python server is done with the following command: `python3 hugin/webserver/zeus.py server`.
The port is configured at the top of `server.py`.

## Generate cryptographic keys

`path/to/Hugin crypto --generateKeys path/to/private/key path/to/public/key`.

## Test the binary

This test doesn't validate whether the binary was tampered with, only that features work as expected.

`path/to/Hugin test`.

# Dependencies & Integrations

## Common

Both Hugin and Munin depend on the following cryptographic libraries:

* the `libhydrogen` library, which is included in this repository as a submodule in `hugin/libhydrogen/`.

* the `sha256` from `mbed TLS` library.

## Hugin

Hugin depends of multiples open-source libraries:

* the `rapidjson` library, which is included in this repository as a submodule in `hugin/thirdparty/`.

* the `bsdiff` tool, which is included directly in `hugin/Scheduler/bsdiff`, with a light modification.

## Munin

Munin is largely an integration reference of our system on as many platforms as possible. Therefore, Munin drivers are a core component of the project. Therefore, the code necessary to make them run is included in this repository as submodules.

### mbedOS

* The reference integration is implemented on the top of mbedOS.

* Network drivers rely on the `easy-connect` component from mbedOS, as a way to keep the minimal network code a generic as possible.

* A modified version of `FreescaleIAP` driver is used to implement flash rewriting.

A `make_mbed.sh` script is available and, assuming it is run from the root of the repository, will build the compilation environment for building Munin on mbedOS.
