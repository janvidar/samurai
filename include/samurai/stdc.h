/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_STDCLIB_H
#define HAVE_SAMURAI_STDCLIB_H

#include <samurai/samurai.h>
#include <string>

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
