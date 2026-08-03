/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/util/string.h>
#include <string>
#include <string_view>
#include <vector>

/*
 * These are the helpers the HTTP header parser, the XML reader and the URL
 * resolver all reach for, so they are asserted here rather than through any of
 * the three. Everything is deterministic; nothing touches the network.
 */

namespace {

using namespace Samurai::Util;

std::string decode(std::string_view text, bool& ok)
{
	std::string out;
	ok = percent_decode(text, out);
	return out;
}

std::string utf8(uint32_t codepoint, bool& ok)
{
	std::string out;
	ok = append_utf8(codepoint, out);
	return out;
}

}

EXO_TEST(string_trim_removes_both_ends,
{
	return trim("  \t urn:schemas-upnp-org \r\n ") == "urn:schemas-upnp-org";
});

EXO_TEST(string_trim_leaves_interior_whitespace,
{
	return trim(" a b ") == "a b";
});

EXO_TEST(string_trim_of_all_whitespace_is_empty,
{
	return trim(" \t\r\n").empty() && trim("").empty();
});

EXO_TEST(string_split_finds_every_field,
{
	const std::vector<std::string_view> fields = split("a,b,c", ',');
	return fields.size() == 3 && fields[0] == "a" && fields[1] == "b" && fields[2] == "c";
});

/* "a,,b" has three fields, and dropping the middle one would silently discard
   an empty header value. */
EXO_TEST(string_split_keeps_empty_fields,
{
	const std::vector<std::string_view> fields = split("a,,b", ',');
	return fields.size() == 3 && fields[1].empty();
});

EXO_TEST(string_split_of_empty_text_is_one_empty_field,
{
	const std::vector<std::string_view> fields = split("", ',');
	return fields.size() == 1 && fields[0].empty();
});

EXO_TEST(string_split_keeps_a_trailing_separator_as_a_field,
{
	const std::vector<std::string_view> fields = split("a,", ',');
	return fields.size() == 2 && fields[1].empty();
});

EXO_TEST(string_split_once_splits_at_the_first_separator,
{
	std::string_view before, after;
	if (!split_once("max-age=1800=x", '=', before, after)) return false;
	return before == "max-age" && after == "1800=x";
});

EXO_TEST(string_split_once_without_a_separator_is_refused,
{
	std::string_view before = "untouched", after = "untouched";
	if (split_once("no colon here", ':', before, after)) return false;
	return before == "untouched" && after == "untouched";
});

EXO_TEST(string_split_once_yields_an_empty_value,
{
	std::string_view before, after;
	return split_once("Empty:", ':', before, after)
		&& before == "Empty" && after.empty();
});

EXO_TEST(string_percent_decode_leaves_plain_text_alone,
{
	bool ok = false;
	return decode("rootDesc.xml", ok) == "rootDesc.xml" && ok;
});

EXO_TEST(string_percent_decode_decodes_an_escape,
{
	bool ok = false;
	return decode("fe80::1%25en0", ok) == "fe80::1%en0" && ok;
});

EXO_TEST(string_percent_decode_accepts_lowercase_hex,
{
	bool ok = false;
	return decode("%2f%2F", ok) == "//" && ok;
});

EXO_TEST(string_percent_decode_rejects_a_truncated_escape,
{
	bool ok = true;
	decode("abc%2", ok);
	if (ok) return false;

	ok = true;
	decode("abc%", ok);
	return !ok;
});

EXO_TEST(string_percent_decode_rejects_non_hex,
{
	bool ok = true;
	decode("%zz", ok);
	if (ok) return false;

	ok = true;
	decode("%2z", ok);
	return !ok;
});

EXO_TEST(string_percent_decode_handles_a_null_byte,
{
	bool ok = false;
	const std::string out = decode("a%00b", ok);
	return ok && out.size() == 3 && out[1] == '\0';
});

EXO_TEST(string_percent_encode_leaves_unreserved_alone,
{
	return percent_encode("AZaz09-._~") == "AZaz09-._~";
});

EXO_TEST(string_percent_encode_escapes_everything_else,
{
	return percent_encode("a b/c") == "a%20b%2Fc";
});

EXO_TEST(string_percent_encode_uses_uppercase_hex,
{
	return percent_encode("\xff") == "%FF";
});

EXO_TEST(string_percent_roundtrip,
{
	const std::string original = "a b/c?d=e&f%g\xc3\xa5";
	bool ok = false;
	return decode(percent_encode(original), ok) == original && ok;
});

EXO_TEST(string_xml_escape_escapes_all_five,
{
	return xml_escape("&<>\"'") == "&amp;&lt;&gt;&quot;&apos;";
});

EXO_TEST(string_xml_escape_leaves_ordinary_text_alone,
{
	return xml_escape("samurai port map") == "samurai port map";
});

EXO_TEST(string_xml_escape_escapes_every_occurrence,
{
	return xml_escape("a&b&c") == "a&amp;b&amp;c";
});

EXO_TEST(string_append_utf8_encodes_ascii,
{
	bool ok = false;
	return utf8('A', ok) == "A" && ok;
});

/* The boundaries between the one, two, three and four byte forms - which is
   where an encoder gets the comparisons wrong. */
EXO_TEST(string_append_utf8_encodes_the_boundary_code_points,
{
	bool ok = false;

	if (utf8(0x7f, ok) != std::string("\x7f") || !ok) return false;
	if (utf8(0x80, ok) != std::string("\xc2\x80") || !ok) return false;
	if (utf8(0x7ff, ok) != std::string("\xdf\xbf") || !ok) return false;
	if (utf8(0x800, ok) != std::string("\xe0\xa0\x80") || !ok) return false;
	if (utf8(0xffff, ok) != std::string("\xef\xbf\xbf") || !ok) return false;
	if (utf8(0x10000, ok) != std::string("\xf0\x90\x80\x80") || !ok) return false;
	if (utf8(0x10ffff, ok) != std::string("\xf4\x8f\xbf\xbf") || !ok) return false;

	return true;
});

EXO_TEST(string_append_utf8_rejects_surrogates,
{
	bool ok = true;
	if (!utf8(0xd800, ok).empty() || ok) return false;

	ok = true;
	if (!utf8(0xdfff, ok).empty() || ok) return false;

	/* The code point either side of the surrogate block is legitimate. */
	ok = false;
	if (utf8(0xd7ff, ok).empty() || !ok) return false;
	ok = false;
	return !utf8(0xe000, ok).empty() && ok;
});

EXO_TEST(string_append_utf8_rejects_above_the_last_code_point,
{
	bool ok = true;
	return utf8(0x110000, ok).empty() && !ok;
});

EXO_TEST(string_append_utf8_appends_rather_than_replaces,
{
	std::string out = "&#";
	return append_utf8(0x41, out) && out == "&#A";
});
