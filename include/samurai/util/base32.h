/*
 * Copyright (C) 2001-2006 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

extern void base32_encode(const unsigned char* buffer, size_t len, char* result);
extern void base32_decode(const char* src, unsigned char* dst, size_t len);

