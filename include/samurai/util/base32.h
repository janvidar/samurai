/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_UTIL_BASE32_H
#define HAVE_SAMURAI_UTIL_BASE32_H

#include <stddef.h>
#include <span>
#include <string_view>

namespace Samurai {
namespace Util {

/**
 * Base32-encode 'input' into 'result', which must have room for
 * base32_encode_size(input.size()) bytes including the terminator.
 *
 * @return the number of characters written, excluding the terminator, or 0 if
 *         the output buffer is too small, in which case 'result' is set to the
 *         empty string. Nothing is written at all only when 'result' is empty.
 */
size_t base32_encode(std::span<const unsigned char> input, std::span<char> result);

/**
 * Bytes needed to hold the encoding of 'len' input bytes, terminator included.
 */
constexpr size_t base32_encode_size(size_t len)
{
	return ((len * 8 + 4) / 5) + 1;
}

/**
 * Base32-decode 'src' into at most dst.size() bytes. Characters outside the
 * alphabet are skipped. Decoding stops at whichever bound is reached first.
 *
 * @return the number of bytes written.
 */
size_t base32_decode(std::string_view src, std::span<unsigned char> dst);

}
}

#endif // HAVE_SAMURAI_UTIL_BASE32_H
