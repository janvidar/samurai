/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_STDCLIB_H
#define HAVE_SAMURAI_STDCLIB_H

#include <samurai/samurai.h>
#include <string>
#include <string_view>

/*
 * NOTE: the C runtime has strcasecmp, strncasecmp, strdup and strcasestr under
 * underscore-prefixed names, so they are mapped rather than reimplemented.
 *
 * strndup is the one genuinely missing function; it exists on Linux, macOS and
 * the BSDs, so the guard is Windows. It is provided under a prefixed name and
 * mapped like strcasestr, because strndup is a name POSIX reserves for the
 * implementation, mingw-w64's string.h does declare it, and defining it here
 * with C linkage would export an unmangled strndup from the shared library that
 * could interpose on the CRT's.
 */
#ifdef SAMURAI_OS_WINDOWS

char* samurai_strndup(const char* s, size_t n);
char* samurai_strcasestr(const char* haystack, const char* needle);

#define strcasecmp  _stricmp
#define strncasecmp _strnicmp
#define strdup      _strdup
#define strcasestr  samurai_strcasestr
#define strndup     samurai_strndup

#endif // SAMURAI_OS_WINDOWS

namespace Samurai {
namespace Util {

unsigned int abs(int n);

/*
 * Case-insensitive comparison over views rather than C strings.
 *
 * These take a length with them, so neither side has to be NUL terminated and
 * neither is measured by walking it - which is what makes them usable on a
 * range inside a buffer that was received rather than built here. The folding
 * is ASCII only and deliberately so: it is protocol keywords, file extensions
 * and header names being compared, and a locale-sensitive tolower() would make
 * the same input compare differently on two machines.
 */
bool iequals(std::string_view a, std::string_view b);
bool istarts_with(std::string_view str, std::string_view prefix);

/** @return the offset of needle in haystack, or std::string_view::npos. */
size_t ifind(std::string_view haystack, std::string_view needle);

inline bool icontains(std::string_view haystack, std::string_view needle) {
	return ifind(haystack, needle) != std::string_view::npos;
}

/*
 * These stop at the first character that is not a digit and saturate on
 * overflow. to_uint16 is the exception and rejects any string that is not
 * entirely digits, which is what url.cpp needs to parse a port.
 */
class Convert
{
	public:
		static uint64_t to_uint64(const char* value);
		static uint64_t to_uint64(const std::string&);
		static int64_t to_int64(const char* value);
		static int64_t to_int64(const std::string&);
		static int32_t to_int32(const char* value);
		static int32_t to_int32(const std::string&);
		static uint16_t to_uint16(const std::string& str);
};

}
}

#endif // HAVE_SAMURAI_STDCLIB_H
