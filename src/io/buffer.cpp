/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <string.h>
#include <samurai/io/buffer.h>
#include <samurai/io/device.h>
#include <stdlib.h>

/**
 * Should add a method called capasity(), which will return the
 * initialized bufsize.
 */
Samurai::IO::Buffer::Buffer(size_t bufsize_) :  buf(0), len(0), bufsize(bufsize_), initialCapasity(bufsize_) {
	buf = (char*) malloc(bufsize);
}

Samurai::IO::Buffer::Buffer(const Samurai::IO::Buffer& copy) : buf(0), len(copy.len), bufsize(copy.bufsize), initialCapasity(copy.initialCapasity) {
	buf = (char*) malloc(bufsize);
	memcpy(buf, copy.buf, len);
}


Samurai::IO::Buffer::Buffer(const Samurai::IO::Buffer* copy) : buf(0), len(copy->len), bufsize(copy->bufsize), initialCapasity(copy->initialCapasity) {
	buf = (char*) malloc(bufsize);
	memcpy(buf, copy->buf, len);
}


Samurai::IO::Buffer::~Buffer() {
	free(buf);
}

void Samurai::IO::Buffer::append(const char* data, size_t len_) {
	if (len + len_ > bufsize) resize(len_);
	memcpy(&buf[len], data, len_);
	len += len_;
}

void Samurai::IO::Buffer::append(const std::string& string) {
	append((char*) string.c_str());
}

void Samurai::IO::Buffer::append(const char* string) {
	size_t len_ = strlen(string);
	if (len + len_ > bufsize) resize(len_);
	memcpy(&buf[len], string, len_);
	len += len_;
}

void Samurai::IO::Buffer::append(char c) {
	if (len + 1 > bufsize) resize(1);
	buf[len++] = c;
}

void Samurai::IO::Buffer::append(int n) {
	const char* num = quickdc_itoa(n, 10);
	append(num, strlen(num));
}

void Samurai::IO::Buffer::append(uint64_t n) {
	const char* num = quickdc_ulltoa(n);
	append(num, strlen(num));
}

void Samurai::IO::Buffer::append(Samurai::IO::Buffer* buffer, size_t len) {
	size_t mylen = (len > buffer->size()) ? buffer->size() : len;
	append(buffer->buf, mylen);
}

void Samurai::IO::Buffer::append(const Buffer& buffer)
{
	append(buffer.buf, buffer.size());
}

void Samurai::IO::Buffer::append(const Buffer& buffer, size_t len)
{
	size_t mylen = (len > buffer.size()) ? buffer.size() : len;
	append(buffer.buf, mylen);
}


void Samurai::IO::Buffer::pop(char* data, size_t len_) {
	if (len == 0) return;
	size_t mylen = (len_ > len) ? len : len_;
	memcpy(data, buf, mylen);
}

void Samurai::IO::Buffer::pop(char* data, size_t offset, size_t len_) {
	if (len == 0) return;
	size_t mylen = (len_ > len) ? len : len_;
	memcpy(data, &buf[offset], mylen);
}

/**
 * Allocate memory and return a chunk based on the
 * given offset and length.
 */
char* Samurai::IO::Buffer::memdup(size_t offset, size_t end) {
	char* temp_buf = (char*) malloc((end-offset)+1);
	memcpy(temp_buf, &buf[offset], end-offset);
	temp_buf[end-offset] = 0;
	return temp_buf;
}


std::string Samurai::IO::Buffer::pop(size_t len_) {
	if (len == 0) return std::string("");
	size_t mylen = (len_ > len) ? len : len_;
	return std::string((char*) buf, mylen);
}

void Samurai::IO::Buffer::remove(size_t count) {
	size_t cnt = (count > len) ? len : count;
	memmove(buf, &buf[cnt], len-cnt);
	len -= cnt;
}

void Samurai::IO::Buffer::resize(size_t size) {
	size_t nsize = bufsize*2;
	if (nsize < bufsize+size) nsize = (bufsize*2)+size;
	
	char* ptr = (char*) realloc(buf, nsize);
	if (ptr) {
		buf = ptr;
		bufsize = nsize;
	}
}

void Samurai::IO::Buffer::clear() {
	char* ptr = (char*) realloc(buf, initialCapasity);
	if (ptr) {
		buf = ptr;
		bufsize = initialCapasity;
	}
	len = 0;
}

int Samurai::IO::Buffer::find(char achar, size_t offset) {
	char* pos = (char*) memchr(&buf[offset], achar, len - offset);
	if (!pos) return -1;
	return &pos[0] - &buf[0];
}

int Samurai::IO::Buffer::rfind(char achar) {
#ifdef LINUX
	char* pos = (char*) memrchr(buf, achar, len);
	if (!pos) return -1;
	return &pos[0] - &buf[0];
#else
	for (size_t x = len; x > 0; x--)
		if (buf[x] == (uint8_t) achar) return x;
	return -1;
#endif
}

int Samurai::IO::Buffer::find(const char* str, size_t offset) {
#ifdef LINUX
	char* pos = (char*) memmem(&buf[offset], len-offset, str, strlen(str));
	if (!pos) return -1;
	return (&pos[0] - &buf[0]);
#else
	int p = offset;
	size_t n = strlen(str);

	if (len - p < n) return -1;

	for (;;) {
		p = find(str[0], p);
		if (p == -1) return -1;
		if (len - p < n) return -1;
		if (memcmp(&buf[offset], str, n) == 0) return p;
		p++;
	}
	return -1;
#endif
}

char* Samurai::IO::Buffer::ptr()
{
	return buf;
}

char Samurai::IO::Buffer::operator[](size_t offset) const {
	return buf[offset];
}

char Samurai::IO::Buffer::at(size_t offset) const {
	return buf[offset];
}

#define SWAP16(x) (\
			(((x) & (uint16_t) 0x00ff) << 8) | \
			(((x) & (uint16_t) 0xff00) >> 8))

#define SWAP32(x) (\
			(((x) & 0x000000ff) << 24) | \
			(((x) & 0x0000ff00) <<  8) | \
			(((x) & 0x00ff0000) >>  8) | \
			(((x) & 0xff000000) >> 24))

#define SWAP64(x) (\
			(((x) & 0x00000000000000ffULL) << 56) | \
			(((x) & 0x000000000000ff00ULL) << 40) | \
			(((x) & 0x0000000000ff0000ULL) << 24) | \
			(((x) & 0x00000000ff000000ULL) <<  8) | \
			(((x) & 0x000000ff00000000ULL) >>  8) | \
			(((x) & 0x0000ff0000000000ULL) >> 24) | \
			(((x) & 0x00ff000000000000ULL) >> 40) | \
			(((x) & 0xff00000000000000ULL) >> 56))

void Samurai::IO::Buffer::appendBinary(uint16_t number_, BinaryMode endiannes) {
	uint16_t number = number_;
	switch (endiannes) {
		case LittleEndian:
#ifdef SAMURAI_BIGENDIAN
			number = SWAP16(number);
#endif
			break;
			
		case BigEndian:
#ifndef SAMURAI_BIGENDIAN
			number = SWAP16(number);
#endif
			break;
		case NativeEndian:
			break;
	}
	 append((char*) &number, sizeof(number));
}

void Samurai::IO::Buffer::appendBinary(uint32_t number_, BinaryMode endiannes) {
	uint32_t number = number_;
	switch (endiannes) {
		case LittleEndian:
#ifdef SAMURAI_BIGENDIAN
			number = SWAP32(number);
#endif
			break;
			
		case BigEndian:
#ifndef SAMURAI_BIGENDIAN
			number = SWAP32(number);
#endif
			break;
		case NativeEndian:
			break;
	}
	 append((char*) &number, sizeof(number));
}

void Samurai::IO::Buffer::appendBinary(uint64_t number_, BinaryMode endiannes) {
	uint64_t number = number_;
	switch (endiannes) {
		case LittleEndian:
#ifdef SAMURAI_BIGENDIAN
			number = SWAP64(number);
#endif
			break;
			
		case BigEndian:
#ifndef SAMURAI_BIGENDIAN
			number = SWAP64(number);
#endif
			break;
		case NativeEndian:
			break;
	}
	 append((char*) &number, sizeof(number));
}

bool Samurai::IO::Buffer::popBinary(size_t offset, uint8_t& number) {
	number = (uint8_t) buf[offset];
	return true;
}
/*
union {
	struct {
		uint8_t d1;
		uint8_t d2;
		uint8_t d3;
		uint8_t d4;
		uint8_t d5;
		uint8_t d6;
		uint8_t d7;
		uint8_t d8;
	} bytes;

	struct {
		uint16_t d1;
		uint16_t d2;
		uint16_t d3;
		uint16_t d4;
	} words;

	struct {
		uint32_t d1;
		uint32_t d2;
	} dwords;

	struct {
		uint64_t d;
	} ddwords;
	
} storage;
*/

bool Samurai::IO::Buffer::popBinary(size_t offset, uint16_t& number, BinaryMode endianness) {
	number = (uint16_t) *reinterpret_cast<uint16_t*>(&buf[offset]);
	switch (endianness) {
		case LittleEndian:
#ifdef SAMURAI_BIGENDIAN
			number = SWAP16(number);
#endif
			break;

		case BigEndian:
#ifndef SAMURAI_BIGENDIAN
			number = SWAP16(number);
#endif
			break;

		case NativeEndian:
			break;
	}

	return true;
}

bool Samurai::IO::Buffer::popBinary(size_t offset, uint32_t& number, BinaryMode endianness) {
	number = (uint32_t) reinterpret_cast<size_t>(&buf[offset]); // FIXME: Is this 32/64 bit safe?
	switch (endianness) {
		case LittleEndian:
#ifdef SAMURAI_BIGENDIAN
			number = SWAP32(number);
#endif
			break;

		case BigEndian:
#ifndef SAMURAI_BIGENDIAN
			number = SWAP32(number);
#endif
			break;

		case NativeEndian:
			break;
	}
	return true;
}

bool Samurai::IO::Buffer::popBinary(size_t offset, uint64_t& number, BinaryMode endianness) {
	number = reinterpret_cast<uint64_t>(&buf[offset]);
	switch (endianness) {
		case LittleEndian:
#ifdef SAMURAI_BIGENDIAN
			number = SWAP64(number);
#endif
			break;

		case BigEndian:
#ifndef SAMURAI_BIGENDIAN
			number = SWAP64(number);
#endif
			break;

		case NativeEndian:
			break;
	}
	return true;
}


