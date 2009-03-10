/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_STDCLIB_H
#define HAVE_SAMURAI_STDCLIB_H

#include <samurai/samurai.h>
#include <string>

#ifndef SAMURAI_OS_LINUX
extern "C" char* strndup(const char *s, size_t n);
#endif

#ifdef SAMURAI_OS_WINDOWS
extern "C" char* strdup(const char* value);
extern "C" int strncmp(const char* s1, const char* s2, size_t n);
extern "C" int strcasecmp(const char* s1, const char* s2);
extern "C" int strncasecmp(const char* s1, const char* s2, size_t n);
extern "C" char* strcasestr(const char* haystack, const char* needle);
// extern int gettimeofday(struct timeval* tv, const struct timezone* tz);
extern "C" int isblank(int c);
#endif // WIN32

extern "C" int64_t quickdc_atoll(const char* value);
extern "C" uint64_t quickdc_atoull(const char* value);
extern "C" int quickdc_atoi(const char* value);

extern "C" unsigned int quickdc_abs(int n);
extern "C" const char* quickdc_itoa(int value, int radix);
extern "C" const char* quickdc_ulltoa(uint64_t value);

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
