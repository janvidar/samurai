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
 * and the BSDs, so the guard is Windows rather than "not Linux".
 */
#ifdef SAMURAI_OS_WINDOWS

extern "C" char* strndup(const char* s, size_t n);
extern "C" char* quickdc_strcasestr(const char* haystack, const char* needle);

#define strcasecmp  _stricmp
#define strncasecmp _strnicmp
#define strdup      _strdup
#define strcasestr  quickdc_strcasestr

#endif // SAMURAI_OS_WINDOWS

extern "C" int64_t quickdc_atoll(const char* value);
extern "C" uint64_t quickdc_atoull(const char* value);
extern "C" int quickdc_atoi(const char* value);

extern "C" unsigned int quickdc_abs(int n);

namespace Samurai {
namespace Util {
class Convert
{
	public:
		static uint64_t to_uint64(const std::string&);
		static int64_t to_int64(const std::string&);
		static uint32_t to_uint32(const std::string&);
		static int32_t to_int32(const std::string&);
		static uint16_t to_uint16(const std::string& str);
/*		
		{
			int n = 0;
			n = quickdc_atoi(str.c_str());
			if (n < 0 || n > 65535) return 0;
			return (uint16_t) n;
		}
*/
		static int16_t to_int16(const std::string&);
};

}
}

#endif // HAVE_SAMURAI_STDCLIB_H
