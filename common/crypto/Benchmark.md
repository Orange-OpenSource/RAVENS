Benchmark:

Default config:
Text 138KB Data 2.61KB BSS 10.4KB ROM 141KB RAM 13KB

  SHA-256                  :       2049 KB/s
  SHA-512                  :        622 KB/s
  AES-CBC-128              :        806 KB/s
  AES-CBC-192              :        688 KB/s
  AES-CBC-256              :        600 KB/s
  AES-GCM-128              :        358 KB/s
  AES-GCM-192              :        332 KB/s
  AES-GCM-256              :        310 KB/s
  AES-CCM-128              :        359 KB/s
  AES-CCM-192              :        311 KB/s
  AES-CCM-256              :        275 KB/s
  CTR_DRBG (NOPR)          :        595 KB/s
  CTR_DRBG (PR)            :        439 KB/s
  HMAC_DRBG SHA-256 (NOPR) :        233 KB/s
  HMAC_DRBG SHA-256 (PR)   :        204 KB/s
  RSA-2048                 :      32 ms/ public
  RSA-2048                 :    1235 ms/private
  RSA-4096                 :     107 ms/ public
  RSA-4096                 :    6538 ms/private
  ECDHE-secp384r1          :    1073 ms/handshake
  ECDHE-secp256r1          :     707 ms/handshake
  ECDHE-Curve25519         :     606 ms/handshake
  ECDH-secp384r1           :     530 ms/handshake
  ECDH-secp256r1           :     349 ms/handshake
  ECDH-Curve25519          :     312 ms/handshake

Trimmed config
Text 92.2KB Data 2.61KB BSS 10.4KB ROM 94.8KB RAM 13KB

  RIPEMD160                :       3766 KB/s
  SHA-256                  :       2040 KB/s
  AES-CTR-128              :        819 KB/s
  AES-CTR-192              :        695 KB/s
  AES-CTR-256              :        602 KB/s
  AES-GCM-128              :        357 KB/s
  AES-GCM-192              :        331 KB/s
  AES-GCM-256              :        309 KB/s
  AES-CCM-128              :        358 KB/s
  AES-CCM-192              :        310 KB/s
  AES-CCM-256              :        272 KB/s
  CTR_DRBG (NOPR)          :        585 KB/s
  CTR_DRBG (PR)            :        447 KB/s
  HMAC_DRBG SHA-256 (NOPR) :        231 KB/s
  HMAC_DRBG SHA-256 (PR)   :        204 KB/s
  ECDHE-Curve25519         :     604 ms/handshake
  ECDH-Curve25519          :     309 ms/handshake

ECDSA:
Text 97.8KB Data 2.63KB BSS 10.3KB ROM 100KB RAM 13KB

  ECDSA-secp521r1          :     841 ms/sign
  ECDSA-secp384r1          :     577 ms/sign
  ECDSA-secp256r1          :     375 ms/sign
  ECDSA-secp256k1          :     356 ms/sign
  ECDSA-secp224r1          :     245 ms/sign
  ECDSA-secp224k1          :     315 ms/sign
  ECDSA-secp192r1          :     189 ms/sign
  ECDSA-secp192k1          :     264 ms/sign
  ECDSA-secp521r1          :    1611 ms/verify
  ECDSA-secp384r1          :    1117 ms/verify
  ECDSA-secp256r1          :     736 ms/verify
  ECDSA-secp256k1          :     690 ms/verify
  ECDSA-secp224r1          :     480 ms/verify
  ECDSA-secp224k1          :     614 ms/verify
  ECDSA-secp192r1          :     364 ms/verify
  ECDSA-secp192k1          :     509 ms/verify

Minimized ECDSA:
Text 76.3KB Data 2.62KB BSS 10.4KB ROM 78.9KB RAM 13KB

  SHA-256                  :       2052 KB/s
  HMAC_DRBG SHA-256 (NOPR) :        232 KB/s
  HMAC_DRBG SHA-256 (PR)   :        204 KB/s
  ECDSA-secp256k1          :     354 ms/sign
  ECDSA-secp224r1          :     247 ms/sign
  ECDSA-secp192r1          :     188 ms/sign
  ECDSA-secp256k1          :     695 ms/verify
  ECDSA-secp224r1          :     480 ms/verify
  ECDSA-secp192r1          :     364 ms/verify

Strictly minimal ECDSA
Text 74.9KB Data 2.62KB BSS 10.4KB ROM 77.5KB RAM 13KB

  SHA-256                  :       2016 KB/s
  HMAC_DRBG SHA-256 (NOPR) :        229 KB/s
  HMAC_DRBG SHA-256 (PR)   :        202 KB/s
  ECDSA-secp256k1          :     356 ms/sign
  ECDSA-secp256k1          :     690 ms/verify

First libHydrogen
Text 77.8KB Data 2.62KB BSS 10.4KB ROM 80.5KB RAM 13KB

  SHA-256                  :       2018 KB/s
  HMAC_DRBG SHA-256 (NOPR) :        228 KB/s
  HMAC_DRBG SHA-256 (PR)   :        201 KB/s
  ECDSA-secp256k1          :     357 ms/sign
  ECDSA-secp256k1          :     692 ms/verify
  libHydrogen              :      40 ms/sign
  libHydrogen              :      68 ms/verify

libHydrogen with random
Text 63.2KB Data 2.61KB BSS 10.4KB ROM 65.8KB RAM 13KB

  SHA-256                  :       2049 KB/s
  HMAC_DRBG SHA-256 (NOPR) :        233 KB/s
  HMAC_DRBG SHA-256 (PR)   :        205 KB/s
  libHydrogen              :      40 ms/signature
  libHydrogen              :      68 ms/verify