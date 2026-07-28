/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_UNICODE_IO_H
#define HAVE_SAMURAI_UNICODE_IO_H

#include <sys/types.h>
#include <samurai/io/codec.h>

namespace Samurai {
namespace IO {

class UnicodePrivate;

class Unicode : public Codec
{
	public:
		Unicode(const char* to, const char* from);
		virtual ~Unicode();
		virtual bool exec(char* input, size_t& input_len, char* output, size_t& output_len);
		virtual bool exec(char* input, size_t& input_len, char* output, size_t& output_len, std::error_code& ec);

	protected:
		UnicodePrivate* cvt;

	private:
		/* Owns an iconv descriptor: copying would close it twice. */
		Unicode(const Unicode&);
		Unicode& operator=(const Unicode&);
};
	

}
}

#endif // HAVE_SAMURAI_UNICODE_IO_H
