cmake_minimum_required (VERSION 2.6)
project (Hermes)

include_directories(libhydrogen)

add_library(cryptoTools signature.c signUtils.c hash.c hashUtils.c crypto.h crypto_cli.c crypto_cli.h
        sha256.min.c sha256.h libhydrogen/hydrogen.c)

set_property(TARGET cryptoTools PROPERTY C_STANDARD 11)

add_library(cryptoCLI crypto.h crypto_cli.c crypto_cli.h)

add_executable(cryptoTest core.c crypto_cli.h)
target_link_libraries(cryptoTest cryptoTools cryptoCLI)
