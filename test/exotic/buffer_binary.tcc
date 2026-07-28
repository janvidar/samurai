/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/buffer.h>
#include <string>
#include <string.h>

/*
 * Every appendBinary()/popBinary() pair has to agree in all three BinaryModes,
 * and the two explicit modes have to produce a specific byte order on the wire
 * rather than merely round trip - a swap that is skipped in both directions
 * round trips perfectly and is still wrong.
 *
 * The expected byte sequences below are therefore written out literally and are
 * the same on every host; only NativeEndian is allowed to differ.
 */

using Mode = Samurai::IO::Buffer::BinaryMode;

static const Mode all_modes[3] = { Mode::BigEndian, Mode::LittleEndian, Mode::NativeEndian };

static bool bytes_are(const Samurai::IO::Buffer& buf, const char* expect, size_t n)
{
	if (buf.size() != n) return false;
	for (size_t i = 0; i < n; i++)
		if ((uint8_t) buf.at(i) != (uint8_t) expect[i]) return false;
	return true;
}

/* ------------------------------------------------------------------------- */
/* Wire order                                                                */
/* ------------------------------------------------------------------------- */

EXO_TEST(binary_append_u16_big_endian_order,
{
	Samurai::IO::Buffer buf;
	buf.appendBinary((uint16_t) 0x1234, Mode::BigEndian);
	return bytes_are(buf, "\x12\x34", 2);
});

EXO_TEST(binary_append_u16_little_endian_order,
{
	Samurai::IO::Buffer buf;
	buf.appendBinary((uint16_t) 0x1234, Mode::LittleEndian);
	return bytes_are(buf, "\x34\x12", 2);
});

EXO_TEST(binary_append_u32_big_endian_order,
{
	Samurai::IO::Buffer buf;
	buf.appendBinary((uint32_t) 0x12345678, Mode::BigEndian);
	return bytes_are(buf, "\x12\x34\x56\x78", 4);
});

EXO_TEST(binary_append_u32_little_endian_order,
{
	Samurai::IO::Buffer buf;
	buf.appendBinary((uint32_t) 0x12345678, Mode::LittleEndian);
	return bytes_are(buf, "\x78\x56\x34\x12", 4);
});

EXO_TEST(binary_append_u64_big_endian_order,
{
	Samurai::IO::Buffer buf;
	buf.appendBinary((uint64_t) 0x0123456789abcdefULL, Mode::BigEndian);
	return bytes_are(buf, "\x01\x23\x45\x67\x89\xab\xcd\xef", 8);
});

EXO_TEST(binary_append_u64_little_endian_order,
{
	Samurai::IO::Buffer buf;
	buf.appendBinary((uint64_t) 0x0123456789abcdefULL, Mode::LittleEndian);
	return bytes_are(buf, "\xef\xcd\xab\x89\x67\x45\x23\x01", 8);
});

/* The two explicit modes must actually disagree, or neither is swapping. */
EXO_TEST(binary_endian_modes_differ,
{
	Samurai::IO::Buffer be;
	Samurai::IO::Buffer le;
	be.appendBinary((uint32_t) 0x11223344, Mode::BigEndian);
	le.appendBinary((uint32_t) 0x11223344, Mode::LittleEndian);
	return be.at(0) != le.at(0) && be.at(0) == le.at(3);
});

/* ------------------------------------------------------------------------- */
/* Reading a known wire encoding                                             */
/* ------------------------------------------------------------------------- */

EXO_TEST(binary_pop_u16_big_endian,
{
	Samurai::IO::Buffer buf;
	buf.append("\x12\x34", 2);
	uint16_t n = 0;
	return buf.popBinary(0, n, Mode::BigEndian) && n == 0x1234;
});

EXO_TEST(binary_pop_u16_little_endian,
{
	Samurai::IO::Buffer buf;
	buf.append("\x12\x34", 2);
	uint16_t n = 0;
	return buf.popBinary(0, n, Mode::LittleEndian) && n == 0x3412;
});

EXO_TEST(binary_pop_u32_big_endian,
{
	Samurai::IO::Buffer buf;
	buf.append("\x12\x34\x56\x78", 4);
	uint32_t n = 0;
	return buf.popBinary(0, n, Mode::BigEndian) && n == 0x12345678;
});

EXO_TEST(binary_pop_u32_little_endian,
{
	Samurai::IO::Buffer buf;
	buf.append("\x12\x34\x56\x78", 4);
	uint32_t n = 0;
	return buf.popBinary(0, n, Mode::LittleEndian) && n == 0x78563412;
});

EXO_TEST(binary_pop_u64_big_endian,
{
	Samurai::IO::Buffer buf;
	buf.append("\x01\x23\x45\x67\x89\xab\xcd\xef", 8);
	uint64_t n = 0;
	return buf.popBinary(0, n, Mode::BigEndian) && n == 0x0123456789abcdefULL;
});

EXO_TEST(binary_pop_u64_little_endian,
{
	Samurai::IO::Buffer buf;
	buf.append("\x01\x23\x45\x67\x89\xab\xcd\xef", 8);
	uint64_t n = 0;
	return buf.popBinary(0, n, Mode::LittleEndian) && n == 0xefcdab8967452301ULL;
});

/* ------------------------------------------------------------------------- */
/* Round trips, including the high bit of every byte                         */
/* ------------------------------------------------------------------------- */

EXO_TEST(binary_round_trip_u16_all_modes,
{
	for (const Mode mode : all_modes)
	{
		Samurai::IO::Buffer buf;
		buf.appendBinary((uint16_t) 0xfedc, mode);
		uint16_t n = 0;
		if (!buf.popBinary(0, n, mode) || n != 0xfedc) return false;
	}
	return true;
});

EXO_TEST(binary_round_trip_u32_all_modes,
{
	for (const Mode mode : all_modes)
	{
		Samurai::IO::Buffer buf;
		buf.appendBinary((uint32_t) 0xfedcba98, mode);
		uint32_t n = 0;
		if (!buf.popBinary(0, n, mode) || n != 0xfedcba98) return false;
	}
	return true;
});

EXO_TEST(binary_round_trip_u64_all_modes,
{
	for (const Mode mode : all_modes)
	{
		Samurai::IO::Buffer buf;
		buf.appendBinary((uint64_t) 0xfedcba9876543210ULL, mode);
		uint64_t n = 0;
		if (!buf.popBinary(0, n, mode) || n != 0xfedcba9876543210ULL) return false;
	}
	return true;
});

EXO_TEST(binary_round_trip_extremes,
{
	Samurai::IO::Buffer buf;
	buf.appendBinary((uint16_t) 0, Mode::BigEndian);
	buf.appendBinary((uint16_t) 0xffff, Mode::BigEndian);
	uint16_t lo = 1;
	uint16_t hi = 0;
	return buf.popBinary(0, lo, Mode::BigEndian) && lo == 0
		&& buf.popBinary(2, hi, Mode::BigEndian) && hi == 0xffff;
});

/* ------------------------------------------------------------------------- */
/* Offsets, uint8_t, and the bounds check                                    */
/* ------------------------------------------------------------------------- */

EXO_TEST(binary_pop_at_offset,
{
	Samurai::IO::Buffer buf;
	buf.append("\xaa\xbb", 2);
	buf.appendBinary((uint32_t) 0xdeadbeef, Mode::BigEndian);
	uint32_t n = 0;
	return buf.popBinary(2, n, Mode::BigEndian) && n == 0xdeadbeef;
});

/* A single byte has no order to it, so it takes no mode. */
EXO_TEST(binary_pop_u8,
{
	Samurai::IO::Buffer buf;
	buf.append("\x80", 1);
	uint8_t n = 0;
	return buf.popBinary(0, n) && n == 0x80;
});

/* popBinary() must leave the destination untouched when it cannot read. */
EXO_TEST(binary_pop_past_end_fails_without_writing,
{
	Samurai::IO::Buffer buf;
	buf.append("\x12", 1);
	uint32_t n = 0x5a5a5a5a;
	return !buf.popBinary(0, n, Mode::BigEndian) && n == 0x5a5a5a5a;
});

EXO_TEST(binary_pop_straddling_the_end_fails,
{
	Samurai::IO::Buffer buf;
	buf.append("\x12\x34\x56", 3);
	uint32_t n = 0x5a5a5a5a;
	return !buf.popBinary(0, n, Mode::BigEndian) && n == 0x5a5a5a5a;
});

EXO_TEST(binary_pop_offset_past_end_fails,
{
	Samurai::IO::Buffer buf;
	buf.append("\x12\x34", 2);
	uint16_t n = 0x5a5a;
	return !buf.popBinary(8, n, Mode::BigEndian) && n == 0x5a5a;
});

EXO_TEST(binary_pop_from_empty_buffer_fails,
{
	Samurai::IO::Buffer buf;
	uint64_t n = 7;
	return !buf.popBinary(0, n, Mode::BigEndian) && n == 7;
});

/* Offsets are relative to the live region, so they have to follow remove(). */
EXO_TEST(binary_offset_follows_remove,
{
	Samurai::IO::Buffer buf;
	buf.append("\xaa\xbb\xcc\xdd", 4);
	buf.appendBinary((uint16_t) 0x1234, Mode::BigEndian);
	buf.remove(4);
	uint16_t n = 0;
	return buf.popBinary(0, n, Mode::BigEndian) && n == 0x1234;
});

/* Reading does not consume, so the same value comes back twice. */
EXO_TEST(binary_pop_does_not_consume,
{
	Samurai::IO::Buffer buf;
	buf.appendBinary((uint32_t) 0x01020304, Mode::BigEndian);
	uint32_t a = 0;
	uint32_t b = 0;
	return buf.popBinary(0, a, Mode::BigEndian)
		&& buf.popBinary(0, b, Mode::BigEndian)
		&& a == b && a == 0x01020304 && buf.size() == 4;
});

/* Several values packed back to back must each read out at their own offset. */
EXO_TEST(binary_sequence_round_trip,
{
	Samurai::IO::Buffer buf;
	buf.appendBinary((uint16_t) 0xbeef, Mode::BigEndian);
	buf.appendBinary((uint32_t) 0xcafebabe, Mode::LittleEndian);
	buf.appendBinary((uint64_t) 0x1122334455667788ULL, Mode::BigEndian);

	uint16_t a = 0;
	uint32_t b = 0;
	uint64_t c = 0;
	return buf.size() == 14
		&& buf.popBinary(0, a, Mode::BigEndian) && a == 0xbeef
		&& buf.popBinary(2, b, Mode::LittleEndian) && b == 0xcafebabe
		&& buf.popBinary(6, c, Mode::BigEndian) && c == 0x1122334455667788ULL;
});
