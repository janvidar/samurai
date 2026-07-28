/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_STDCLIB_H
#define HAVE_SAMURAI_STDCLIB_H

#include <samurai/samurai.h>
#include <string>

/*
 * NOTE: this block used to declare strdup, strncmp, strcasecmp, strncasecmp,
 * strcasestr and isblank as functions this library provides. Only strdup was
 * ever defined, so the rest were link errors waiting for someone to build on
 * Windows - and strncmp and isblank are standard C that is always present
 * anyway. The C runtime has the others under underscore-prefixed names, so
 * they are mapped rather than reimplemented.
 *
 * strndup is the one genuinely missing function; it exists on Linux, macOS
 * and the BSDs, so the guard is Windows rather than "not Linux". It is
 * provided under a prefixed name and mapped like strcasestr: strndup is a
 * name POSIX reserves for the implementation, mingw-w64's string.h does
 * declare it, and defining it here with C linkage exported an unmangled
 * strndup from the shared library that could interpose on the CRT's.
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
 * NOTE: the const char* overloads were the free functions samurai_atoll,
 * samurai_atoull and samurai_atoi, declared extern "C". Nothing in either
 * project is C, so the linkage only cost them the namespace and exported them
 * unmangled. The std::string overloads of to_int64, to_uint64 and to_int32
 * were declared here but never defined - link errors waiting for a caller -
 * and now delegate. to_uint32 and to_int16 were declared and never defined or
 * called either, and are gone.
 *
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
