/*
 * Copyright (C) 2001-2006 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_UTIL_BASE32_H
#define HAVE_SAMURAI_UTIL_BASE32_H

#include <stddef.h>

/**
 * Base32-encode 'len' bytes into 'result', which must have room for
 * base32_encode_size(len) bytes including the terminator.
 *
 * @return the number of characters written, excluding the terminator, or 0 if
 *         the output buffer is too small. Nothing is written in that case.
 *
 * NOTE: this used to take no output size at all, so the caller's buffer was
 * all that stood between the encoder and the rest of the stack.
 */
extern size_t base32_encode(const unsigned char* buffer, size_t len,
                            char* result, size_t result_len);

/**
 * Bytes needed to hold the encoding of 'len' input bytes, terminator included.
 */
extern size_t base32_encode_size(size_t len);

/**
 * Base32-decode 'src' into at most 'len' bytes of 'dst'.
 * @return the number of bytes written.
 */
extern size_t base32_decode(const char* src, unsigned char* dst, size_t len);

#endif // HAVE_SAMURAI_UTIL_BASE32_H
