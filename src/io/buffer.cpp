/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <string.h>
#include <samurai/io/buffer.h>
#include <string>
#include <string_view>
#include <bit>
#include <array>
#include <concepts>
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

Samurai::IO::Buffer::Buffer(size_t bufsize_) : buf(bufsize_), head(0), len(0), initialCapasity(bufsize_) {
}

Samurai::IO::Buffer::Buffer(const Samurai::IO::Buffer& copy) : buf(copy.buf), head(copy.head), len(copy.len), initialCapasity(copy.initialCapasity) {
}


Samurai::IO::Buffer::Buffer(const Samurai::IO::Buffer* copy) : buf(copy->buf), head(copy->head), len(copy->len), initialCapasity(copy->initialCapasity) {
}

Samurai::IO::Buffer::Buffer(Samurai::IO::Buffer&& other) noexcept
	: buf(std::move(other.buf)), head(other.head), len(other.len), initialCapasity(other.initialCapasity) {
	other.head = 0;
	other.len = 0;
}


Samurai::IO::Buffer& Samurai::IO::Buffer::operator=(const Samurai::IO::Buffer& copy) {
	if (this == &copy) return *this;
	buf = copy.buf;
	head = copy.head;
	len = copy.len;
	initialCapasity = copy.initialCapasity;
	return *this;
}


Samurai::IO::Buffer& Samurai::IO::Buffer::operator=(Samurai::IO::Buffer&& other) noexcept {
	if (this == &other) return *this;
	buf = std::move(other.buf);
	head = other.head;
	len = other.len;
	initialCapasity = other.initialCapasity;
	other.head = 0;
	other.len = 0;
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

void Samurai::IO::Buffer::append(const std::string_view string) {
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
	if (mylen) append(&buffer->buf[buffer->head], mylen);
}

void Samurai::IO::Buffer::append(const Buffer& buffer)
{
	if (buffer.size()) append(&buffer.buf[buffer.head], buffer.size());
}

void Samurai::IO::Buffer::append(const Buffer& buffer, size_t len)
{
	size_t mylen = (len > buffer.size()) ? buffer.size() : len;
	if (mylen) append(&buffer.buf[buffer.head], mylen);
}


size_t Samurai::IO::Buffer::pop(char* data, size_t len_) {
	const size_t live = len - head;
	if (live == 0) return 0;
	size_t mylen = (len_ > live) ? live : len_;
	memcpy(data, &buf[head], mylen);
	return mylen;
}

size_t Samurai::IO::Buffer::pop(char* data, size_t offset, size_t len_) {
	const size_t live = len - head;
	if (offset >= live) return 0;
	size_t available = live - offset;
	size_t mylen = (len_ > available) ? available : len_;
	memcpy(data, &buf[head + offset], mylen);
	return mylen;
}

/**
 * Allocate memory and return a chunk based on the
 * given offset and length.
 * Returns 0 if the range is outside the buffer, or on allocation failure.
 */
std::string Samurai::IO::Buffer::copyRange(size_t offset, size_t end) const {
	const size_t live = len - head;
	if (offset > end || end > live) return std::string();

	return std::string(&buf[head + offset], end - offset);
}

char* Samurai::IO::Buffer::memdup(size_t offset, size_t end) {
	const size_t live = len - head;
	if (offset > end || end > live) return nullptr;

	size_t size = end - offset;
	char* temp_buf = (char*) malloc(size + 1);
	if (!temp_buf) return nullptr;

	memcpy(temp_buf, &buf[head + offset], size);
	temp_buf[size] = 0;
	return temp_buf;
}


std::string Samurai::IO::Buffer::pop(size_t len_) {
	const size_t live = len - head;
	if (live == 0) return std::string("");
	size_t mylen = (len_ > live) ? live : len_;
	return std::string(&buf[head], mylen);
}

void Samurai::IO::Buffer::remove(size_t count) {
	const size_t live = len - head;
	const size_t cnt = (count > live) ? live : count;

	head += cnt;

	/* Everything consumed: start over at the front rather than drifting. */
	if (head == len) { head = 0; len = 0; return; }

	/* Otherwise only pay for a move once the dead prefix dominates. */
	if (head >= (buf.size() / 2)) compact();
}

void Samurai::IO::Buffer::compact() {
	if (!head) return;

	const size_t live = len - head;
	if (live) memmove(&buf[0], &buf[head], live);
	head = 0;
	len = live;
}


void Samurai::IO::Buffer::reserve(size_t n) {
	if (n) resize(n);
}


bool Samurai::IO::Buffer::resize(size_t needed) {
	/* Reclaim the consumed prefix before asking for more memory: a long-lived
	   connection would otherwise keep growing the allocation while most of it
	   sat behind 'head'. */
	if (head && len + needed > buf.size()) compact();

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
	head = 0;
	len = 0;
}

size_t Samurai::IO::Buffer::find(char achar, size_t offset) {
	const size_t live = len - head;
	if (offset >= live) return npos;

	char* pos = (char*) memchr(&buf[head + offset], achar, live - offset);
	if (!pos) return npos;
	return (size_t) (pos - &buf[head]);
}

size_t Samurai::IO::Buffer::rfind(char achar) {
	const size_t live = len - head;
	if (live == 0) return npos;

#ifdef SAMURAI_HAVE_GNU_MEMSEARCH
	char* pos = (char*) memrchr(&buf[head], achar, live);
	if (!pos) return npos;
	return (size_t) (pos - &buf[head]);
#else
	/* NOTE: x is unsigned - 'x-- > 0' walks len-1 down to 0 inclusive. */
	for (size_t x = live; x-- > 0; )
		if (buf[head + x] == achar) return x;
	return npos;
#endif
}

size_t Samurai::IO::Buffer::find(const char* str, size_t offset) {
	size_t n = strlen(str);
	const size_t live = len - head;

	if (n == 0) return (offset <= live) ? offset : npos;
	if (offset >= live) return npos;
	if (live - offset < n) return npos;

#ifdef SAMURAI_HAVE_GNU_MEMSEARCH
	char* pos = (char*) memmem(&buf[head + offset], live - offset, str, n);
	if (!pos) return npos;
	return (size_t) (pos - &buf[head]);
#else
	size_t p = offset;
	for (;;) {
		size_t found = find(str[0], p);
		if (found == npos) return npos;

		p = found;
		if (live - p < n) return npos;

		/* NOTE: compare at p, not at the starting offset. */
		if (memcmp(&buf[head + p], str, n) == 0) return p;
		p++;
	}
#endif
}

char* Samurai::IO::Buffer::ptr()
{
	return buf.empty() ? 0 : &buf[head];
}

char Samurai::IO::Buffer::operator[](size_t offset) const {
	return buf[head + offset];
}

char Samurai::IO::Buffer::at(size_t offset) const {
	if (offset >= len - head) return 0;
	return buf[head + offset];
}

/*
 * NOTE: endianness is taken from std::endian, which the compiler supplies, so a
 * misspelling here is a build error rather than byte swapping that silently does
 * nothing on a host nobody tests on.
 */
static constexpr bool host_is_big_endian = (std::endian::native == std::endian::big);

namespace {

template<typename T>
concept ByteOrdered = std::same_as<T, uint16_t>
                   || std::same_as<T, uint32_t>
                   || std::same_as<T, uint64_t>;

/* std::byteswap is C++23 and this library builds as C++20, so the swap is
   spelled out. Both GCC and Clang fold this loop to a single instruction. */
template<ByteOrdered T>
constexpr T byteswap(T value) noexcept
{
	T swapped = 0;
	for (size_t n = 0; n < sizeof(T); n++)
	{
		swapped = (T) ((swapped << 8) | (value & 0xff));
		value >>= 8;
	}
	return swapped;
}

/* Swapping is its own inverse, so one function serves both directions. */
template<ByteOrdered T>
constexpr T reorder(T value, Samurai::IO::Buffer::BinaryMode mode) noexcept
{
	using Mode = Samurai::IO::Buffer::BinaryMode;

	if (mode == Mode::BigEndian)
		return host_is_big_endian ? value : byteswap(value);

	if (mode == Mode::LittleEndian)
		return host_is_big_endian ? byteswap(value) : value;

	return value;
}

/* bit_cast rather than a cast to char*, which reads the object through an
   unrelated type. */
template<ByteOrdered T>
void appendValue(Samurai::IO::Buffer& target, T number, Samurai::IO::Buffer::BinaryMode mode)
{
	const auto bytes = std::bit_cast<std::array<char, sizeof(T)>>(reorder(number, mode));
	target.append(bytes.data(), bytes.size());
}

}

void Samurai::IO::Buffer::appendBinary(uint16_t number, BinaryMode endiannes) {
	appendValue(*this, number, endiannes);
}

void Samurai::IO::Buffer::appendBinary(uint32_t number, BinaryMode endiannes) {
	appendValue(*this, number, endiannes);
}

void Samurai::IO::Buffer::appendBinary(uint64_t number, BinaryMode endiannes) {
	appendValue(*this, number, endiannes);
}

/*
 * NOTE: These read the *value stored at* the offset.
 *
 * memcpy() is used rather than a cast through a uint32_t*, since the offset
 * carries no alignment guarantee and type-punning through a pointer is
 * undefined behaviour.
 */

bool Samurai::IO::Buffer::popBinary(size_t offset, uint8_t& number) {
	if (offset > (len - head) || (len - head) - offset < sizeof(number)) return false;

	memcpy(&number, &buf[head + offset], sizeof(number));
	return true;
}

bool Samurai::IO::Buffer::popBinary(size_t offset, uint16_t& number, BinaryMode endianness) {
	if (offset > (len - head) || (len - head) - offset < sizeof(number)) return false;

	uint16_t wire;
	memcpy(&wire, &buf[head + offset], sizeof(wire));
	number = reorder(wire, endianness);
	return true;
}

bool Samurai::IO::Buffer::popBinary(size_t offset, uint32_t& number, BinaryMode endianness) {
	if (offset > (len - head) || (len - head) - offset < sizeof(number)) return false;

	uint32_t wire;
	memcpy(&wire, &buf[head + offset], sizeof(wire));
	number = reorder(wire, endianness);
	return true;
}

bool Samurai::IO::Buffer::popBinary(size_t offset, uint64_t& number, BinaryMode endianness) {
	if (offset > (len - head) || (len - head) - offset < sizeof(number)) return false;

	uint64_t wire;
	memcpy(&wire, &buf[head + offset], sizeof(wire));
	number = reorder(wire, endianness);
	return true;
}

