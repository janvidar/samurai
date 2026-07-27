/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <string.h>
#include <samurai/io/buffer.h>
#include <string>
#include <bit>
#include <samurai/io/device.h>
#include <stdlib.h>
#include <new>

/*
 * memrchr() and memmem() are glibc extensions. g++ defines _GNU_SOURCE on
 * glibc targets, but check for it rather than assume it - the portable
 * fallbacks below are equivalent, only slower.
 */
#if defined(__GLIBC__) && defined(_GNU_SOURCE)
#define SAMURAI_HAVE_GNU_MEMSEARCH
#endif

Samurai::IO::Buffer::Buffer(size_t bufsize_) : buf(bufsize_), len(0), initialCapasity(bufsize_) {
}

Samurai::IO::Buffer::Buffer(const Samurai::IO::Buffer& copy) : buf(copy.buf), len(copy.len), initialCapasity(copy.initialCapasity) {
}


Samurai::IO::Buffer::Buffer(const Samurai::IO::Buffer* copy) : buf(copy->buf), len(copy->len), initialCapasity(copy->initialCapasity) {
}

Samurai::IO::Buffer& Samurai::IO::Buffer::operator=(const Samurai::IO::Buffer& copy) {
	if (this == &copy) return *this;
	buf = copy.buf;
	len = copy.len;
	initialCapasity = copy.initialCapasity;
	return *this;
}


Samurai::IO::Buffer::~Buffer() {
}

void Samurai::IO::Buffer::append(const char* data, size_t len_) {
	if (len + len_ > buf.size() && !resize(len_)) {
		QERR("Buffer::append: unable to grow buffer, dropping %lu bytes", (unsigned long) len_);
		return;
	}
	memcpy(&buf[len], data, len_);
	len += len_;
}

void Samurai::IO::Buffer::append(const std::string& string) {
	/* NOTE: Must not go via the strlen() overload - a std::string is
	   allowed to contain embedded NUL bytes. */
	append(string.data(), string.size());
}

void Samurai::IO::Buffer::append(const char* string) {
	append(string, strlen(string));
}

void Samurai::IO::Buffer::append(char c) {
	if (len + 1 > buf.size() && !resize(1)) {
		QERR("Buffer::append: unable to grow buffer, dropping 1 byte");
		return;
	}
	buf[len++] = c;
}

void Samurai::IO::Buffer::append(int n) {
	const std::string num = std::to_string(n);
	append(num.data(), num.size());
}

void Samurai::IO::Buffer::append(uint64_t n) {
	const std::string num = std::to_string(n);
	append(num.data(), num.size());
}

void Samurai::IO::Buffer::append(Samurai::IO::Buffer* buffer, size_t len) {
	size_t mylen = (len > buffer->size()) ? buffer->size() : len;
	if (mylen) append(&buffer->buf[0], mylen);
}

void Samurai::IO::Buffer::append(const Buffer& buffer)
{
	if (buffer.size()) append(&buffer.buf[0], buffer.size());
}

void Samurai::IO::Buffer::append(const Buffer& buffer, size_t len)
{
	size_t mylen = (len > buffer.size()) ? buffer.size() : len;
	if (mylen) append(&buffer.buf[0], mylen);
}


void Samurai::IO::Buffer::pop(char* data, size_t len_) {
	if (len == 0) return;
	size_t mylen = (len_ > len) ? len : len_;
	memcpy(data, &buf[0], mylen);
}

void Samurai::IO::Buffer::pop(char* data, size_t offset, size_t len_) {
	if (offset >= len) return;
	size_t available = len - offset;
	size_t mylen = (len_ > available) ? available : len_;
	memcpy(data, &buf[offset], mylen);
}

/**
 * Allocate memory and return a chunk based on the
 * given offset and length.
 * Returns 0 if the range is outside the buffer, or on allocation failure.
 */
char* Samurai::IO::Buffer::memdup(size_t offset, size_t end) {
	if (offset > end || end > len) return 0;

	size_t size = end - offset;
	char* temp_buf = (char*) malloc(size + 1);
	if (!temp_buf) return 0;

	memcpy(temp_buf, &buf[offset], size);
	temp_buf[size] = 0;
	return temp_buf;
}


std::string Samurai::IO::Buffer::pop(size_t len_) {
	if (len == 0) return std::string("");
	size_t mylen = (len_ > len) ? len : len_;
	return std::string(&buf[0], mylen);
}

void Samurai::IO::Buffer::remove(size_t count) {
	size_t cnt = (count > len) ? len : count;
	if (len > cnt) memmove(&buf[0], &buf[cnt], len-cnt);
	len -= cnt;
}

bool Samurai::IO::Buffer::resize(size_t needed) {
	size_t required = len + needed;
	if (required < len) return false; /* size_t overflow */
	if (required <= buf.size()) return true;

	size_t nsize = buf.size() ? buf.size() : INITBUFSIZE;
	while (nsize < required) {
		size_t next = nsize * 2;
		if (next <= nsize) { /* size_t overflow */
			nsize = required;
			break;
		}
		nsize = next;
	}

	try {
		buf.resize(nsize);
	} catch (const std::bad_alloc&) {
		return false;
	}
	return true;
}

void Samurai::IO::Buffer::clear() {
	buf.assign(initialCapasity, 0);
	len = 0;
}

int Samurai::IO::Buffer::find(char achar, size_t offset) {
	if (offset >= len) return -1;

	char* pos = (char*) memchr(&buf[offset], achar, len - offset);
	if (!pos) return -1;
	return (int) (pos - &buf[0]);
}

int Samurai::IO::Buffer::rfind(char achar) {
	if (len == 0) return -1;

#ifdef SAMURAI_HAVE_GNU_MEMSEARCH
	char* pos = (char*) memrchr(&buf[0], achar, len);
	if (!pos) return -1;
	return (int) (pos - &buf[0]);
#else
	/* NOTE: x is unsigned - 'x-- > 0' walks len-1 down to 0 inclusive. */
	for (size_t x = len; x-- > 0; )
		if (buf[x] == achar) return (int) x;
	return -1;
#endif
}

int Samurai::IO::Buffer::find(const char* str, size_t offset) {
	size_t n = strlen(str);

	if (n == 0) return (offset <= len) ? (int) offset : -1;
	if (offset >= len) return -1;
	if (len - offset < n) return -1;

#ifdef SAMURAI_HAVE_GNU_MEMSEARCH
	char* pos = (char*) memmem(&buf[offset], len - offset, str, n);
	if (!pos) return -1;
	return (int) (pos - &buf[0]);
#else
	size_t p = offset;
	for (;;) {
		int found = find(str[0], p);
		if (found == -1) return -1;

		p = (size_t) found;
		if (len - p < n) return -1;

		/* NOTE: compare at p, not at the starting offset. */
		if (memcmp(&buf[p], str, n) == 0) return (int) p;
		p++;
	}
#endif
}

char* Samurai::IO::Buffer::ptr()
{
	return buf.empty() ? 0 : &buf[0];
}

char Samurai::IO::Buffer::operator[](size_t offset) const {
	return buf[offset];
}

char Samurai::IO::Buffer::at(size_t offset) const {
	return buf[offset];
}

#define SWAP16(x) ((uint16_t) (\
			(((x) & (uint16_t) 0x00ff) << 8) | \
			(((x) & (uint16_t) 0xff00) >> 8)))

#define SWAP32(x) ((uint32_t) (\
			(((x) & 0x000000ffUL) << 24) | \
			(((x) & 0x0000ff00UL) <<  8) | \
			(((x) & 0x00ff0000UL) >>  8) | \
			(((x) & 0xff000000UL) >> 24)))

#define SWAP64(x) ((uint64_t) (\
			(((x) & 0x00000000000000ffULL) << 56) | \
			(((x) & 0x000000000000ff00ULL) << 40) | \
			(((x) & 0x0000000000ff0000ULL) << 24) | \
			(((x) & 0x00000000ff000000ULL) <<  8) | \
			(((x) & 0x000000ff00000000ULL) >>  8) | \
			(((x) & 0x0000ff0000000000ULL) >> 24) | \
			(((x) & 0x00ff000000000000ULL) >> 40) | \
			(((x) & 0xff00000000000000ULL) >> 56)))

/*
 * NOTE: This used to be #ifdef SAMURAI_BIG_ENDIAN around every conversion, and
 * before that a misspelled SAMURAI_BIGENDIAN that is defined nowhere - so the
 * byte swapping silently did nothing on big endian hosts and no compiler ever
 * said so. std::endian is a value the compiler supplies, so a typo here is a
 * build error rather than a wrong result on a machine nobody tests on.
 */
static constexpr bool host_is_big_endian = (std::endian::native == std::endian::big);

void Samurai::IO::Buffer::appendBinary(uint16_t number_, BinaryMode endiannes) {
	uint16_t number = number_;
	switch (endiannes) {
		case LittleEndian:
			if constexpr (host_is_big_endian) number = SWAP16(number);
			break;

		case BigEndian:
			if constexpr (!host_is_big_endian) number = SWAP16(number);
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
			if constexpr (host_is_big_endian) number = SWAP32(number);
			break;

		case BigEndian:
			if constexpr (!host_is_big_endian) number = SWAP32(number);
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
			if constexpr (host_is_big_endian) number = SWAP64(number);
			break;

		case BigEndian:
			if constexpr (!host_is_big_endian) number = SWAP64(number);
			break;
		case NativeEndian:
			break;
	}
	 append((char*) &number, sizeof(number));
}

/*
 * NOTE: These read the *value stored at* the offset. The uint32/uint64
 * variants used to return reinterpret_cast<...>(&buf[offset]) - that is, the
 * address of the slot rather than its contents.
 *
 * memcpy() is used rather than a cast through a uint32_t*, since the offset
 * carries no alignment guarantee and type-punning through a pointer is
 * undefined behaviour.
 */

bool Samurai::IO::Buffer::popBinary(size_t offset, uint8_t& number) {
	if (offset > len || len - offset < sizeof(number)) return false;

	memcpy(&number, &buf[offset], sizeof(number));
	return true;
}

bool Samurai::IO::Buffer::popBinary(size_t offset, uint16_t& number, BinaryMode endianness) {
	if (offset > len || len - offset < sizeof(number)) return false;

	memcpy(&number, &buf[offset], sizeof(number));
	switch (endianness) {
		case LittleEndian:
			if constexpr (host_is_big_endian) number = SWAP16(number);
			break;

		case BigEndian:
			if constexpr (!host_is_big_endian) number = SWAP16(number);
			break;

		case NativeEndian:
			break;
	}

	return true;
}

bool Samurai::IO::Buffer::popBinary(size_t offset, uint32_t& number, BinaryMode endianness) {
	if (offset > len || len - offset < sizeof(number)) return false;

	memcpy(&number, &buf[offset], sizeof(number));
	switch (endianness) {
		case LittleEndian:
			if constexpr (host_is_big_endian) number = SWAP32(number);
			break;

		case BigEndian:
			if constexpr (!host_is_big_endian) number = SWAP32(number);
			break;

		case NativeEndian:
			break;
	}
	return true;
}

bool Samurai::IO::Buffer::popBinary(size_t offset, uint64_t& number, BinaryMode endianness) {
	if (offset > len || len - offset < sizeof(number)) return false;

	memcpy(&number, &buf[offset], sizeof(number));
	switch (endianness) {
		case LittleEndian:
			if constexpr (host_is_big_endian) number = SWAP64(number);
			break;

		case BigEndian:
			if constexpr (!host_is_big_endian) number = SWAP64(number);
			break;

		case NativeEndian:
			break;
	}
	return true;
}

