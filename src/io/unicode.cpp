/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <errno.h>
#include <samurai/io/unicode.h>

#ifdef HAVE_ICONV

#if defined(BSD) || defined(SOLARIS)
#define ICONV_CAST (const char**)
#else
#define ICONV_CAST
#endif

#include <iconv.h>

namespace Samurai {
namespace IO {
class UnicodePrivate
{
	public:
		UnicodePrivate(const char* to, const char* from)
		{
			cd = iconv_open(to, from);
		}

		~UnicodePrivate()
		{
			iconv_close(cd);
		}

		bool convert(char* in, size_t& inlen, char* out, size_t& outlen)
		{
        		if (cd == (iconv_t) (-1))
				return false;
			int ret = iconv(cd, ICONV_CAST &in, &inlen, &out, &outlen);
		        if (ret == -1 && errno != E2BIG)
				return false;
			return true;
		}

	protected:
		iconv_t cd;
};
}
}
#else

namespace Samurai {
namespace IO {
class UnicodePrivate
{
	public:
	UnicodePrivate(const char*, const char*) { }
	~UnicodePrivate() { }
	bool convert(char*, size_t&, char*, size_t&) { return false; }
};
}
}
#endif

Samurai::IO::Unicode::Unicode(const char* to, const char* from)
{
	cvt = new UnicodePrivate(to, from);
}

Samurai::IO::Unicode::~Unicode()
{
	delete cvt;
}

bool Samurai::IO::Unicode::exec(char* in, size_t& inlen, char* out, size_t& outlen)
{
	return cvt->convert(in, inlen, out, outlen);
}

