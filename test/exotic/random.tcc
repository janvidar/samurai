/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

/*
 * Samurai::Util::secureRandom() and constantTimeEquals().
 *
 * A generator cannot be tested for being unpredictable, so what is checked here
 * is everything around that: that it succeeds at all on this platform, that it
 * writes the whole range it was given and no more, that it does not simply
 * repeat itself, and that a caller asking for nothing is not told the platform
 * failed. The statistical cases are deliberately weak - they catch a source that
 * returned a constant or left the buffer untouched, which is the failure mode
 * that matters, and would not notice a subtly biased one.
 */

#include <samurai/util/random.h>

#include <set>
#include <span>
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

namespace {

using Samurai::Util::constantTimeEquals;
using Samurai::Util::secureRandom;

std::span<const uint8_t> bytes_of(const std::string& s)
{
	return std::span<const uint8_t>(
		reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

}

/* ------------------------------------------------------------------------- */
/* secureRandom                                                              */
/* ------------------------------------------------------------------------- */

/*
 * The one that matters most: whatever source this platform selected has to
 * actually work, because the alternative is that every caller fails closed and
 * nothing that needs a token can be done at all.
 */
EXO_TEST(random_secure_succeeds_on_this_platform,
{
	uint8_t buffer[32];
	return secureRandom(buffer, sizeof(buffer));
});

EXO_TEST(random_secure_fills_the_whole_range,
{
	/* Pre-set to a value the generator would have to overwrite, with a guard byte
	   either side so writing outside the span is visible. */
	uint8_t buffer[34];
	memset(buffer, 0xAA, sizeof(buffer));

	if (!secureRandom(std::span<uint8_t>(buffer + 1, 32))) return false;

	if (buffer[0] != 0xAA || buffer[33] != 0xAA) return false;

	/* Every byte still 0xAA would mean nothing was written. One or two by chance
	   is fine; thirty-two is not. */
	size_t untouched = 0;
	for (size_t n = 1; n < 33; n++) if (buffer[n] == 0xAA) untouched++;
	return untouched < 32;
});

/* A request larger than getentropy()'s 256-byte limit, which is served in
   pieces - so the seam between them is worth crossing. */
EXO_TEST(random_secure_serves_a_request_over_the_chunk_limit,
{
	std::vector<uint8_t> buffer(1000, 0);
	if (!secureRandom(std::span<uint8_t>(buffer))) return false;

	/* A short zero run is unremarkable; a whole chunk of them means a piece was
	   skipped. Checked across the 256 boundary in particular. */
	size_t longest = 0, run = 0;
	for (uint8_t b : buffer)
	{
		run = b ? 0 : run + 1;
		if (run > longest) longest = run;
	}
	return longest < 32;
});

EXO_TEST(random_secure_does_not_repeat_itself,
{
	/* Sixteen bytes each: two equal draws would be a coincidence worth failing
	   over, and an unseeded or constant source produces nothing else. */
	std::set<std::string> seen;
	for (int n = 0; n < 64; n++)
	{
		uint8_t buffer[16];
		if (!secureRandom(buffer, sizeof(buffer))) return false;
		seen.insert(std::string(reinterpret_cast<const char*>(buffer), sizeof(buffer)));
	}
	return seen.size() == 64;
});

/* Asking for nothing is a request that was satisfied, not a source that failed:
   a caller looping over a zero-length range should not have to special-case it. */
EXO_TEST(random_secure_accepts_an_empty_range,
{
	uint8_t unused = 0;
	return secureRandom(std::span<uint8_t>(&unused, 0))
		&& secureRandom(nullptr, 0);
});

/* But a null destination with a length is a caller bug, not something to write
   through. */
EXO_TEST(random_secure_refuses_a_null_buffer,
{
	return !secureRandom(nullptr, 16);
});

/* ------------------------------------------------------------------------- */
/* constantTimeEquals                                                        */
/* ------------------------------------------------------------------------- */

EXO_TEST(random_constant_time_equals_matches_identical_ranges,
{
	const std::string a = "a token that is long enough to be worth comparing";
	const std::string b = a;
	return constantTimeEquals(bytes_of(a), bytes_of(b));
});

EXO_TEST(random_constant_time_equals_separates_a_difference_anywhere,
{
	const std::string base = "0123456789abcdef";

	/* First byte, last byte, and one in the middle: an implementation that
	   compares a prefix, or stops early, gets one of these wrong. */
	for (size_t at : { size_t(0), size_t(8), base.size() - 1 })
	{
		std::string other = base;
		other[at] = static_cast<char>(other[at] ^ 0x01);
		if (constantTimeEquals(bytes_of(base), bytes_of(other))) return false;
	}
	return true;
});

/* A prefix is not a match, in either direction. */
EXO_TEST(random_constant_time_equals_rejects_a_different_length,
{
	const std::string full = "secret-token";
	const std::string prefix = "secret";

	return !constantTimeEquals(bytes_of(full), bytes_of(prefix))
		&& !constantTimeEquals(bytes_of(prefix), bytes_of(full));
});

EXO_TEST(random_constant_time_equals_handles_empty_ranges,
{
	const std::string empty;
	const std::string one = "x";

	return constantTimeEquals(bytes_of(empty), bytes_of(empty))
		&& !constantTimeEquals(bytes_of(empty), bytes_of(one));
});

/* A single differing bit is what a near-miss guess looks like. */
EXO_TEST(random_constant_time_equals_notices_one_bit,
{
	uint8_t a[16];
	if (!secureRandom(a, sizeof(a))) return false;

	uint8_t b[16];
	memcpy(b, a, sizeof(b));
	b[15] = static_cast<uint8_t>(b[15] ^ 0x80);

	return constantTimeEquals(std::span<const uint8_t>(a, sizeof(a)),
			std::span<const uint8_t>(b, sizeof(b))) == false
		&& constantTimeEquals(std::span<const uint8_t>(a, sizeof(a)),
			std::span<const uint8_t>(a, sizeof(a)));
});

EXO_TEST(random_constant_time_equals_on_strings,
{
	return constantTimeEquals("QDCtoken", "QDCtoken")
		&& !constantTimeEquals("QDCtoken", "QDCtokeN")
		&& !constantTimeEquals("QDCtoken", "QDCtoke")
		/* A null is not equal to anything, including another null: there is no
		   secret there to have matched. */
		&& !constantTimeEquals(nullptr, "x")
		&& !constantTimeEquals("x", nullptr)
		&& !constantTimeEquals(nullptr, nullptr);
});
