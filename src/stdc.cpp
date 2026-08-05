/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <algorithm>
#include <samurai/samurai.h>
#include <samurai/stdc.h>
#include <stdlib.h>
#include <string.h>

/*
 * ASCII only, and taking an unsigned char: the ctype functions are defined for
 * EOF and for unsigned char values, so handing them a plain char sign-extends
 * every byte above 0x7f into a negative index. UTF-8 text is full of those.
 */
static char samurai_ascii_lower(char ch) {
	const unsigned char c = (unsigned char) ch;
	return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : ch;
}

bool Samurai::Util::iequals(std::string_view a, std::string_view b) {
	if (a.size() != b.size()) return false;

	for (size_t n = 0; n < a.size(); n++)
		if (samurai_ascii_lower(a[n]) != samurai_ascii_lower(b[n])) return false;

	return true;
}

bool Samurai::Util::istarts_with(std::string_view str, std::string_view prefix) {
	if (prefix.size() > str.size()) return false;
	return Samurai::Util::iequals(str.substr(0, prefix.size()), prefix);
}

size_t Samurai::Util::ifind(std::string_view haystack, std::string_view needle) {
	/* Everything contains the empty string, at the front - as strstr has it. */
	if (needle.empty()) return 0;
	if (needle.size() > haystack.size()) return std::string_view::npos;

	const size_t last = haystack.size() - needle.size();
	for (size_t at = 0; at <= last; at++)
		if (Samurai::Util::iequals(haystack.substr(at, needle.size()), needle))
			return at;

	return std::string_view::npos;
}

static const char* samurai_skip_ws_sign(const char* value, bool* negative) {
	*negative = false;
	if (!value) return nullptr;

	while (*value == ' ' || *value == '\t' || *value == '\n' ||
	       *value == '\r' || *value == '\v' || *value == '\f')
		value++;

	if (*value == '-') { *negative = true; value++; }
	else if (*value == '+') { value++; }

	return value;
}

/**
 * A very simple string to (64 bit) integer converter.
 * Stops at the first character that is not a digit. Overflow saturates.
 */
int64_t Samurai::Util::Convert::to_int64(const char* value) {
	bool negative = false;
	const char* p = samurai_skip_ws_sign(value, &negative);
	if (!p) return 0;

	uint64_t val = 0;
	const uint64_t limit = negative ? 9223372036854775808ULL /* -INT64_MIN */
	                                : 9223372036854775807ULL /*  INT64_MAX */;

	for (; *p >= '0' && *p <= '9'; p++) {
		const unsigned digit = (unsigned) (*p - '0');
		if (val > (limit - digit) / 10) { val = limit; break; }
		val = val * 10 + digit;
	}

	if (negative)
		return (val == 9223372036854775808ULL) ? (-9223372036854775807LL - 1)
		                                       : -(int64_t) val;
	return (int64_t) val;
}

/**
 * A very simple string to (64 bit) integer converter.
 */
uint64_t Samurai::Util::Convert::to_uint64(const char* value) {
	bool negative = false;
	const char* p = samurai_skip_ws_sign(value, &negative);
	if (!p || negative) return 0;

	uint64_t val = 0;
	for (; *p >= '0' && *p <= '9'; p++) {
		const unsigned digit = (unsigned) (*p - '0');
		if (val > (18446744073709551615ULL - digit) / 10) return 18446744073709551615ULL;
		val = val * 10 + digit;
	}
	return val;
}

/**
 * A very simple string to (64 bit) integer converter.
 */
int32_t Samurai::Util::Convert::to_int32(const char* value) {
	const int64_t val = to_int64(value);
	if (val > 2147483647LL)  return 2147483647;
	if (val < -2147483648LL) return (-2147483647 - 1);
	return (int32_t) val;
}

uint64_t Samurai::Util::Convert::to_uint64(const std::string& str) {
	return to_uint64(str.c_str());
}

int64_t Samurai::Util::Convert::to_int64(const std::string& str) {
	return to_int64(str.c_str());
}

int32_t Samurai::Util::Convert::to_int32(const std::string& str) {
	return to_int32(str.c_str());
}

unsigned int Samurai::Util::abs(int n) {
	return (n < 0) ? (0u - (unsigned int) n) : (unsigned int) n;
}

/* strndup exists on Linux, macOS and the BSDs; only Windows lacks it. */
#ifdef SAMURAI_OS_WINDOWS
char* samurai_strndup(const char *value, size_t len) {
	char* dupval = (char*) malloc(len+1);
	strncpy(dupval, value, len);
	dupval[len] = 0;
	return dupval;
}
#endif

#ifdef SAMURAI_OS_WINDOWS
char* samurai_strcasestr(const char* haystack, const char* needle) {
	if (!haystack || !needle) return 0;

	const size_t nlength = strlen(needle);
	const size_t hlength = strlen(haystack);

	if (nlength == 0) return (char*) haystack;
	if (nlength > hlength) return 0;

	for (size_t i = 0; i <= (hlength - nlength); i++) {
		if (_strnicmp(haystack + i, needle, nlength) == 0)
			return (char*) (haystack + i);
	}
	return 0;
}
#endif

uint16_t Samurai::Util::Convert::to_uint16(const std::string& str)
{
	if (str.empty()) return 0;

	if (!std::ranges::all_of(str, [](const char c) { return c >= '0' && c <= '9'; }))
		return 0;

	const int64_t n = to_int64(str.c_str());
	if (n < 0 || n > 65535) return 0;
	return (uint16_t) n;
}

