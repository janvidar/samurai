/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <errno.h>
#include <samurai/io/unicode.h>
#include <memory>
#include <samurai/error.h>

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
			if (cd != (iconv_t) (-1))
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
	cvt = std::make_unique<UnicodePrivate>(to, from);
}

Samurai::IO::Unicode::~Unicode()
{
}

Samurai::IO::Codec::Progress Samurai::IO::Unicode::internal_step(
	std::span<const char> input, std::span<char> output, std::error_code& ec)
{
	Progress progress;

	/* iconv reports what is left rather than what it took, and takes a
	   mutable pointer to input it only reads. */
	char* in = const_cast<char*>(input.data());
	char* out = output.data();
	size_t inleft = input.size();
	size_t outleft = output.size();

	errno = 0;
	if (!cvt->convert(in, inleft, out, outleft))
	{
		/* iconv sets EILSEQ for invalid input, EINVAL for a truncated
		   multibyte sequence and E2BIG for a full output buffer. */
		ec = Samurai::system_error(errno ? errno : EIO);
		return progress;
	}

	progress.consumed = input.size() - inleft;
	progress.produced = output.size() - outleft;
	/* A converter has no end of stream of its own; it is done when the
	   caller stops feeding it. */
	progress.status = Status::Ok;
	return progress;
}
