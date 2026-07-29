/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_UNICODE_IO_H
#define HAVE_SAMURAI_UNICODE_IO_H

#include <sys/types.h>
#include <samurai/io/codec.h>
#include <memory>

namespace Samurai {
namespace IO {

class UnicodePrivate;

class Unicode final : public Codec
{
	public:
		Unicode(const char* to, const char* from);
		~Unicode() override;
		/* Owns an iconv descriptor: copying would close it twice. */
		Unicode(const Unicode&) = delete;
		Unicode& operator=(const Unicode&) = delete;

	protected:
		Progress internal_step(std::span<const char> input, std::span<char> output,
		                       std::error_code& ec) override;

		std::unique_ptr<UnicodePrivate> cvt;
};
	

}
}

#endif // HAVE_SAMURAI_UNICODE_IO_H
