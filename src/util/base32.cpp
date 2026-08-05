/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <sys/types.h>
#include <string.h>

#include <samurai/util/base32.h>
#include <span>

#define HASH 40

static const char* ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";


size_t Samurai::Util::base32_encode(std::span<const unsigned char> input, std::span<char> out) {
	const unsigned char* buffer = input.data();
	const size_t len = input.size();
	char* result = out.data();
	const size_t result_len = out.size();

	if (!result || !result_len) return 0;
	if (result_len < Samurai::Util::base32_encode_size(len)) { result[0] = '\0'; return 0; }
	if (!buffer || !len) { result[0] = '\0'; return 0; }

	unsigned char word = 0;
	size_t n = 0;
	for (size_t i = 0, index = 0; i < len;) {
		if (index > 3) {
			word = (buffer[i] & (0xFF >> index));
			index = (index + 5) % 8;
			word <<= index;
			if (i < len - 1)
				word |= buffer[i + 1] >> (8 - index);
			i++;
		} else {
			word = (buffer[i] >> (8 - (index + 5))) & 0x1F;
			index = (index + 5) % 8;
			if (index == 0) i++;
		}
		result[n++] = ALPHABET[word];
	}
	result[n] = '\0';
	return n;
}

size_t Samurai::Util::base32_decode(std::string_view source, std::span<unsigned char> out) {
	const char* src = source.data();
	unsigned char* dst = out.data();
	const size_t len = out.size();

	size_t index = 0;
	size_t offset = 0;
	if (!src || !dst || !len) return 0;
	memset(dst, 0, len);

	/* Bounded by the view, which carries its own length and need not be NUL
	   terminated - a view over part of a larger buffer has no terminator at
	   its end. */
	for (size_t i = 0; i < source.size(); i++) {
		unsigned char n = 0;
		for (; n < 32; n++) if (src[i] == ALPHABET[n]) break;
		if (n == 32) continue;
		if (index <= 3) {
			index = (index + 5) % 8;
			if (index == 0) {
				dst[offset++] |= n;
				if (offset == len) break;
			} else {
				dst[offset] |= n << (8 - index);
			}
		} else {
			index = (index + 5) % 8;
			dst[offset++] |= (n >> index);
			if (offset == len) break;
			dst[offset] |= n << (8 - index);
		}
	}
	return offset;
}
