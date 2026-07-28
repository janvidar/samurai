/*
 * Copyright (C) 2001-2006 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_CODEC_IO_H
#define HAVE_SAMURAI_CODEC_IO_H

#include <samurai/samurai.h>
#include <samurai/error.h>

namespace Samurai {
namespace IO {

class Codec {
	public:
		virtual ~Codec();
		virtual bool exec(char* input, size_t& input_len, char* output, size_t& output_len) = 0;

		/**
		 * As exec(), but reporting why on failure.
		 *
		 * The default forwards to the bool overload and reports a generic
		 * EIO; implementations that can say more override it.
		 */
		virtual bool exec(char* input, size_t& input_len,
		                  char* output, size_t& output_len, std::error_code& ec);
};
	
}
}

#endif // HAVE_SAMURAI_CODEC_IO_H
