/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/stream.h>

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

Samurai::IO::Stream::Stream(int f) : flags(f)
{
}

Samurai::IO::Stream::~Stream()
{
}

ssize_t Samurai::IO::Stream::read(char* data, size_t length)
{
	(void) data;
	(void) length;
	return -1;
}

ssize_t Samurai::IO::Stream::write(const char* data, size_t length)
{
	(void) data;
	(void) length;
	return -1;
}

size_t Samurai::IO::Stream::size()
{
	return 0;
}

bool Samurai::IO::Stream::setPosition(uint64_t offset)
{
	(void) offset;
	return false;
}

uint64_t Samurai::IO::Stream::getPosition()
{
	return 0;
}

bool Samurai::IO::Stream::flush()
{
	return false;
}

bool Samurai::IO::Stream::isSeekable() const
{
	return flags & Seekable;
}

bool Samurai::IO::Stream::isReadable() const
{
	return flags & Read;
}

bool Samurai::IO::Stream::isWritable() const
{
	return flags & Write;
}




ssize_t Samurai::IO::Stream::write(const char* str)
{
	return write(str, strlen(str));
}

ssize_t Samurai::IO::Stream::write(const std::string& str)
{
	return write(str.c_str(), str.size());
}

ssize_t Samurai::IO::Stream::write(char chr)
{
	return write(&chr, 1);
}

ssize_t Samurai::IO::Stream::writeText(uint8_t num)
{
	(void) num;
	return -1; // FIXME: Implement me
}

ssize_t Samurai::IO::Stream::writeText(uint16_t num)
{
	(void) num;
	return -1; // FIXME: Implement me
}

ssize_t Samurai::IO::Stream::writeText(uint32_t num)
{
	(void) num;
	return -1; // FIXME: Implement me
}

ssize_t Samurai::IO::Stream::writeText(uint64_t num)
{
	(void) num;
	return -1; // FIXME: Implement me
}

ssize_t Samurai::IO::Stream::writeText(int8_t num)
{
	(void) num;
	return -1; // FIXME: Implement me
}

ssize_t Samurai::IO::Stream::writeText(int16_t num)
{
	(void) num;
	return -1; // FIXME: Implement me
}

ssize_t Samurai::IO::Stream::writeText(int32_t num)
{
	(void) num;
	return -1; // FIXME: Implement me
}

ssize_t Samurai::IO::Stream::writeText(int64_t num)
{
	(void) num;
	return -1; // FIXME: Implement me
}

ssize_t Samurai::IO::Stream::writeBinary(uint16_t num, BinaryMode endian)
{
#ifdef SAMURAI_BIGENDIAN
	if (endian == LittleEndian)
#else
	if (endian == BigEndian)
#endif
		num = SWAP16(num);
	return write((char*) &num, sizeof(num));
}

ssize_t Samurai::IO::Stream::writeBinary(uint32_t num, BinaryMode endian)
{
#ifdef SAMURAI_BIGENDIAN
	if (endian == LittleEndian)
#else
	if (endian == BigEndian)
#endif
		num = SWAP32(num);
	return write((char*) &num, sizeof(num));
}

ssize_t Samurai::IO::Stream::writeBinary(uint64_t num, BinaryMode endian)
{
#ifdef SAMURAI_BIGENDIAN
	if (endian == LittleEndian)
#else
	if (endian == BigEndian)
#endif
		num = SWAP64(num);
	return write((char*) &num, sizeof(num));
}

ssize_t Samurai::IO::Stream::writeBinary(int16_t num, BinaryMode endian)
{
#ifdef SAMURAI_BIGENDIAN
	if (endian == LittleEndian)
#else
	if (endian == BigEndian)
#endif
		num = SWAP16(num);
	return write((char*) &num, sizeof(num));
}

ssize_t Samurai::IO::Stream::writeBinary(int32_t num, BinaryMode endian)
{
#ifdef SAMURAI_BIGENDIAN
	if (endian == LittleEndian)
#else
	if (endian == BigEndian)
#endif
		num = SWAP32(num);
	return write((char*) &num, sizeof(num));
}

ssize_t Samurai::IO::Stream::writeBinary(int64_t num, BinaryMode endian)
{
#ifdef SAMURAI_BIGENDIAN
	if (endian == LittleEndian)
#else
	if (endian == BigEndian)
#endif
		num = SWAP64(num);
	return write((char*) &num, sizeof(num));
}

