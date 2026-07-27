/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/stdc.h>
#include <stdlib.h>
#include <string.h>

static const char* quickdc_skip_ws_sign(const char* value, bool* negative) {
	*negative = false;
	if (!value) return 0;

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
int64_t quickdc_atoll(const char* value) {
	bool negative = false;
	const char* p = quickdc_skip_ws_sign(value, &negative);
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
uint64_t quickdc_atoull(const char* value) {
	bool negative = false;
	const char* p = quickdc_skip_ws_sign(value, &negative);
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
int quickdc_atoi(const char* value) {
	const int64_t val = quickdc_atoll(value);
	if (val > 2147483647LL)  return 2147483647;
	if (val < -2147483648LL) return (-2147483647 - 1);
	return (int) val;
}

unsigned int quickdc_abs(int n) {
	return (n < 0) ? (0u - (unsigned int) n) : (unsigned int) n;
}

/* NOTE: strdup is no longer reimplemented here; stdc.h maps it to the CRT's
   _strdup, which avoids colliding with the one Windows already provides. */

/* NOTE: this was #ifndef SAMURAI_OS_LINUX, which redefined a function libc
   already provides on macOS and the BSDs. Only Windows lacks it. */
#ifdef SAMURAI_OS_WINDOWS
char *strndup(const char *value, size_t len) {
	char* dupval = (char*) malloc(len+1);
	strncpy(dupval, value, len);
	dupval[len] = 0;
	return dupval;
}
#endif

#ifdef SAMURAI_OS_WINDOWS
char* quickdc_strcasestr(const char* haystack, const char* needle) {
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

	for (std::string::const_iterator it = str.begin(); it != str.end(); ++it)
		if (*it < '0' || *it > '9') return 0;

	const int64_t n = quickdc_atoll(str.c_str());
	if (n < 0 || n > 65535) return 0;
	return (uint16_t) n;
}

