/*
 * Copyright (c) 2009 Andrew Collette <andrew.collette at gmail.com>
 * http://lzfx.googlecode.com
 *
 * Implements an LZF-compatible compressor/decompressor based on the liblzf
 * codebase written by Marc Lehmann.  This code is released under the BSD
 * license.  License and original copyright statement follow.
 *
 *
 * Copyright (c) 2000-2008 Marc Alexander Lehmann <schmorp@schmorp.de>
 *
 * Redistribution and use in source and binary forms, with or without modifica-
 * tion, are permitted provided that the following conditions are met:
 *
 *   1.  Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *
 *   2.  Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MER-
 * CHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPE-
 * CIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTH-
 * ERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef LZFX4K_H
#define LZFX4K_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

/* Hashtable size (2**LZFX_HLOG entries) */
#ifndef LZFX_HLOG_4K
# define LZFX_HLOG_4K 16u
#endif

/* Predefined errors. */
#define LZFX_ESIZE      (-1)      /* Output buffer too small */
#define LZFX_ECORRUPT   (-2)      /* Invalid data for decompression */
#define LZFX_EARGS      (-3)      /* Arguments invalid (NULL) */

/*  Buffer-to buffer compression.

    Supply pre-allocated input and output buffers via ibuf and obuf, and
    their size in bytes via ilen and olen.  Buffers may not overlap.

    On success, the function returns a non-negative value and the argument
    olen contains the compressed size in bytes.  On failure, a negative
    value is returned and olen is not modified.
*/
int lzfx_compress(const void* ibuf, size_t ilen, void* obuf, size_t *olen);

/*  Buffer-to-buffer decompression.

    Supply pre-allocated input and output buffers via ibuf and obuf, and
    their size in bytes via ilen and olen.  Buffers may not overlap.

    On success, the function returns a non-negative value and the argument
    olen contains the uncompressed size in bytes.  On failure, a negative
    value is returned.

    If the failure code is LZFX_ESIZE, olen contains the minimum buffer size
    required to hold the decompressed data.  Otherwise, olen is not modified.

    Supplying a zero *olen is a valid and supported strategy to determine the
    required buffer size.  This does not require decompression of the entire
    stream and is consequently very fast.  Argument obuf may be NULL in
    this case only.
*/
int lzfx_decompress(const void* ibuf, size_t ilen, void* obuf, size_t *olen);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
