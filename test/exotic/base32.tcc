/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/util/base32.h>
#include <string>
#include <vector>
#include <string.h>

/*
 * The alphabet is RFC 4648's, so the published vectors apply - except that this
 * encoder does not pad, so the trailing '=' of each is dropped.
 *
 * base32_decode() is reached by nothing else in either tree; these are its only
 * callers.
 */

static bool b32_encodes(const char* input, const char* expect)
{
	const size_t len = strlen(input);
	std::vector<char> out(Samurai::Util::base32_encode_size(len));
	const size_t written = Samurai::Util::base32_encode(
		std::span<const unsigned char>((const unsigned char*) input, len), out);

	return written == strlen(expect) && strcmp(out.data(), expect) == 0;
}

static bool b32_round_trips(const char* input)
{
	const size_t len = strlen(input);
	std::vector<char> encoded(Samurai::Util::base32_encode_size(len));
	if (!Samurai::Util::base32_encode(
		std::span<const unsigned char>((const unsigned char*) input, len), encoded) && len)
		return false;

	std::vector<unsigned char> decoded(len ? len : 1);
	const size_t got = Samurai::Util::base32_decode(encoded.data(), decoded);

	return got == len && memcmp(decoded.data(), input, len) == 0;
}

/* ------------------------------------------------------------------------- */
/* RFC 4648 vectors, unpadded                                                */
/* ------------------------------------------------------------------------- */

EXO_TEST(base32_encode_f,      { return b32_encodes("f", "MY"); });
EXO_TEST(base32_encode_fo,     { return b32_encodes("fo", "MZXQ"); });
EXO_TEST(base32_encode_foo,    { return b32_encodes("foo", "MZXW6"); });
EXO_TEST(base32_encode_foob,   { return b32_encodes("foob", "MZXW6YQ"); });
EXO_TEST(base32_encode_fooba,  { return b32_encodes("fooba", "MZXW6YTB"); });
EXO_TEST(base32_encode_foobar, { return b32_encodes("foobar", "MZXW6YTBOI"); });

EXO_TEST(base32_decode_foobar,
{
	unsigned char out[16];
	const size_t n = Samurai::Util::base32_decode("MZXW6YTBOI", out);
	return n == 6 && memcmp(out, "foobar", 6) == 0;
});

/* ------------------------------------------------------------------------- */
/* Round trips                                                               */
/* ------------------------------------------------------------------------- */

EXO_TEST(base32_round_trip_f,      { return b32_round_trips("f"); });
EXO_TEST(base32_round_trip_foobar, { return b32_round_trips("foobar"); });
EXO_TEST(base32_round_trip_long,
{
	return b32_round_trips("the quick brown fox jumps over the lazy dog");
});

EXO_TEST(base32_round_trip_binary,
{
	unsigned char input[256];
	for (size_t n = 0; n < sizeof(input); n++) input[n] = (unsigned char) n;

	std::vector<char> encoded(Samurai::Util::base32_encode_size(sizeof(input)));
	if (!Samurai::Util::base32_encode(input, encoded)) return false;

	unsigned char decoded[256];
	const size_t got = Samurai::Util::base32_decode(encoded.data(), decoded);

	return got == sizeof(input) && memcmp(decoded, input, sizeof(input)) == 0;
});

/* ------------------------------------------------------------------------- */
/* Bounds                                                                    */
/* ------------------------------------------------------------------------- */

EXO_TEST(base32_encode_size_counts_the_terminator,
{
	return Samurai::Util::base32_encode_size(0) == 1
		&& Samurai::Util::base32_encode_size(1) == 3
		&& Samurai::Util::base32_encode_size(5) == 9
		&& Samurai::Util::base32_encode_size(6) == 11;
});

/* Too small must write nothing rather than truncate. */
EXO_TEST(base32_encode_rejects_a_short_buffer,
{
	char out[4];
	memset(out, 'x', sizeof(out));
	const unsigned char input[] = "foobar";

	const size_t n = Samurai::Util::base32_encode(
		std::span<const unsigned char>(input, 6), std::span<char>(out, sizeof(out)));

	/* Terminated rather than left holding a partial encoding. */
	return n == 0 && out[0] == '\0';
});

/* An exactly-sized buffer must succeed; it is the boundary the check guards. */
EXO_TEST(base32_encode_accepts_an_exact_buffer,
{
	const unsigned char input[] = "foobar";
	char out[11];
	const size_t n = Samurai::Util::base32_encode(
		std::span<const unsigned char>(input, 6), std::span<char>(out, sizeof(out)));

	return n == 10 && strcmp(out, "MZXW6YTBOI") == 0;
});

EXO_TEST(base32_decode_stops_at_the_output_bound,
{
	unsigned char out[3];
	const size_t n = Samurai::Util::base32_decode("MZXW6YTBOI", out);
	return n == 3 && memcmp(out, "foo", 3) == 0;
});

EXO_TEST(base32_decode_empty_input,
{
	unsigned char out[8];
	return Samurai::Util::base32_decode("", out) == 0;
});

EXO_TEST(base32_decode_into_empty_buffer,
{
	return Samurai::Util::base32_decode("MZXW6YTBOI", std::span<unsigned char>()) == 0;
});

/* Characters outside the alphabet are skipped, not decoded. */
EXO_TEST(base32_decode_ignores_foreign_characters,
{
	unsigned char out[16];
	const size_t n = Samurai::Util::base32_decode("MZXW-6YTB OI!", out);
	return n == 6 && memcmp(out, "foobar", 6) == 0;
});

/*
 * A string_view carries its own length and need not be NUL terminated. Reading
 * to a terminator instead runs off the end of a view over a larger buffer.
 */
EXO_TEST(base32_decode_respects_the_view_length,
{
	const char encoded[] = "MZXW6YTBOI";
	unsigned char out[16];

	/* The first five characters are "foo"; the rest of the buffer must not be
	   consulted even though it is there and NUL terminated further along. */
	const size_t n = Samurai::Util::base32_decode(std::string_view(encoded, 5), out);
	return n == 3 && memcmp(out, "foo", 3) == 0;
});

EXO_TEST(base32_decode_view_of_unterminated_storage,
{
	/* No NUL anywhere in this array. */
	char raw[10];
	memcpy(raw, "MZXW6YTBOI", 10);

	unsigned char out[16];
	const size_t n = Samurai::Util::base32_decode(std::string_view(raw, 10), out);
	return n == 6 && memcmp(out, "foobar", 6) == 0;
});
