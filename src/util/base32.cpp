/*
 * Copyright (C) 2001-2006 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <sys/types.h>
#include <string.h>

#include <samurai/util/base32.h>

#define HASH 40

static const char* ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

void base32_encode(const unsigned char* buffer, size_t len, char* result) {
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
}

void base32_decode(const char* src, unsigned char* dst, size_t len) {
	size_t index = 0;
	size_t offset = 0;
	memset(dst, 0, len);
	for (size_t i = 0; src[i]; i++) {
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
}

