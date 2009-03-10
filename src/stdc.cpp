/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/stdc.h>
#include <stdlib.h>
#include <string.h>

/**
 * A very simple string to (64 bit) integer converter.
 */
int64_t quickdc_atoll(const char* value) {
	int len = strlen(value);
	int offset = 0;
	int64_t val = 0;
	for (int i = 0; i < len; i++)
		if (value[i] > '9' || value[i] < '0') 
			offset++;
			
	for (int i = offset; i< len; i++) 
		val = val*10 + (value[i] - '0');
		
	return value[0] == '-' ? -val : val;
}

/**
 * A very simple string to (64 bit) integer converter.
 */
uint64_t quickdc_atoull(const char* value) {
	int len = strlen(value);
	int offset = 0;
	uint64_t val = 0;
	for (int i = 0; i < len; i++)
		if (value[i] > '9' || value[i] < '0') 
			offset++;
			
	for (int i = offset; i< len; i++) 
		val = val*10 + (value[i] - '0');
		
	return val;
}

/**
 * A very simple string to (64 bit) integer converter.
 */
int quickdc_atoi(const char* value) {
	int len = strlen(value);
	int offset = 0;
	int val = 0;
	for (int i = 0; i < len; i++)
		if (value[i] > '9' || value[i] < '0') 
			offset++;
			
	for (int i = offset; i< len; i++) 
		val = val*10 + (value[i] - '0');
		
	return value[0] == '-' ? -val : val;
}

/**
 * Works with radix <= 16.
 * FIXME: How about negative numbers?
 */
const char* quickdc_itoa(int value, int radix) {
	static char buf[32] = { 0 };
	size_t i = 0;
	if (value == 0) return "0";
	int val = quickdc_abs(value);
	for(i = 30; val && i; --i, val /= radix) buf[i] = "0123456789abcdef"[val % radix];
	return &buf[i+1];
}

const char* quickdc_ulltoa(uint64_t value) {
	static char buf[21] = { 0 };
	size_t i = 0;
	if (value == 0) return "0";
	for(i = 20; value && i; --i, value /= 10) buf[i] = "0123456789"[value % 10];
	return &buf[i+1];
}

unsigned int quickdc_abs(int n) {
	return (n < 0) ? -n : n;
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
	int n = 0;
	n = quickdc_atoi(str.c_str());
	if (n < 0 || n > 65535) return 0;
	return (uint16_t) n;
}

