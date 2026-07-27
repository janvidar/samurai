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

/**
 * Works with radix 2..16.
 */
const char* quickdc_itoa(int value, int radix) {
	static char buf[36] = { 0 };

	if (radix < 2 || radix > 16) return "";
	if (value == 0) return "0";

	const bool negative = (value < 0);
	unsigned int val = negative ? (0u - (unsigned int) value)
	                            : (unsigned int) value;

	size_t i = sizeof(buf) - 1;
	buf[i] = '\0';

	while (val && i) {
		buf[--i] = "0123456789abcdef"[val % (unsigned int) radix];
		val /= (unsigned int) radix;
	}

	if (negative && i) buf[--i] = '-';

	return &buf[i];
}

const char* quickdc_ulltoa(uint64_t value) {
	static char buf[24] = { 0 };

	if (value == 0) return "0";

	size_t i = sizeof(buf) - 1;
	buf[i] = '\0';

	while (value && i) {
		buf[--i] = "0123456789"[value % 10];
		value /= 10;
	}

	return &buf[i];
}

unsigned int quickdc_abs(int n) {
	return (n < 0) ? (0u - (unsigned int) n) : (unsigned int) n;
}

#ifdef SAMURAI_OS_WINDOWS
char* strdup(const char* value) {
	int len = strlen(value);
	char* dupval = (char*) malloc(len+1);
	strcpy(dupval, value);
	dupval[len] = '\0';
	return dupval;
}
#endif // WIN32

#ifndef SAMURAI_OS_LINUX
char *strndup(const char *value, size_t len) {
	char* dupval = (char*) malloc(len+1);
	strncpy(dupval, value, len);
	dupval[len] = 0;
	return dupval;
}
#endif

#ifdef SAMURAI_OS_WINDOWS
char* quickdc_strcasestr(char* haystack, char* needle) {
	int nlength = (int) strlen (needle);
	int hlength = (int) strlen (haystack);

	if (nlength > hlength) return 0;
	if (hlength <= 0) return 0;
	if (nlength <= 0) return haystack;
	for (int i = 0; i <= (hlength - nlength); i++) {
		if (strncasecmp (haystack + i, needle, nlength) == 0)
			return haystack + i;
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

