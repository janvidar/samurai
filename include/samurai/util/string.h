/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_UTIL_STRING_H
#define HAVE_SAMURAI_UTIL_STRING_H

#include <samurai/stdc.h>

#include <stdint.h>
#include <string>
#include <string_view>
#include <vector>

namespace Samurai {
namespace Util {

/*
 * NOTE: the case-insensitive comparisons live in samurai/stdc.h, beside the
 * other Samurai::Util helpers - iequals(), istarts_with(), ifind() and
 * icontains(). What is here is what those do not cover.
 */

/**
 * The text with leading and trailing whitespace removed.
 *
 * Whitespace is space, tab, CR and LF - what HTTP calls OWS and what XML
 * allows around an element's character data. The result points into 'text'.
 */
std::string_view trim(std::string_view text);

/**
 * Split on every occurrence of 'sep'.
 *
 * Empty fields are kept, so "a,,b" has three of them and "" has one. The
 * results point into 'text', which has to outlive them.
 */
std::vector<std::string_view> split(std::string_view text, char sep);

/**
 * Split at the first occurrence of 'sep'.
 *
 * @return false if there is none, leaving 'before' and 'after' untouched. This
 *         is what separates a header line from a line that merely contains no
 *         colon, which the caller has to reject rather than guess at.
 */
bool split_once(std::string_view text, char sep,
                std::string_view& before, std::string_view& after);

/**
 * RFC 3986 percent-decoding.
 *
 * @return false on a truncated or non-hexadecimal escape, leaving 'out' with
 *         whatever had been decoded up to that point. A caller that cares only
 *         whether the input was well formed can ignore it.
 */
bool percent_decode(std::string_view text, std::string& out);

/** Percent-encode everything outside the RFC 3986 unreserved set. */
std::string percent_encode(std::string_view text);

/**
 * Escape for XML character data and for a double-quoted attribute value: the
 * five predefined entities.
 *
 * This is the whole of the XML the library writes. A SOAP body is a handful of
 * values in a fixed envelope, so a writer would be more code than escaping
 * them here, and it keeps samurai/io/xml.h read-only - which is worth having
 * in something that parses input from an unauthenticated device.
 */
std::string xml_escape(std::string_view text);

/**
 * Append 'codepoint' to 'out' as UTF-8.
 *
 * @return false for a surrogate or anything above U+10FFFF, appending nothing.
 *         Neither can be encoded, and a numeric character reference naming one
 *         has to be refused rather than turned into replacement bytes.
 */
bool append_utf8(uint32_t codepoint, std::string& out);

}
}

#endif // HAVE_SAMURAI_UTIL_STRING_H
