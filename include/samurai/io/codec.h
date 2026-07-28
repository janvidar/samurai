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
		/**
		 * Outcome of one exec() step.
		 *
		 * The bool-returning overloads cannot tell these apart: a codec that has
		 * finished and a codec that failed both stop returning true, and a flush
		 * that needs several calls consumes nothing on each of them. A caller
		 * driving a codec to completion needs this.
		 */
		enum class Status
		{
			Ok,        /**< progress was made; call again */
			StreamEnd, /**< the stream is complete; do not call again */
			Error      /**< failed; see the error_code overload for why */
		};

		virtual ~Codec();

		/**
		 * Transform input into output.
		 *
		 * @param input_len  in: bytes available. out: bytes consumed.
		 * @param output_len in: room available. out: bytes produced.
		 *
		 * Passing zero available input asks the codec to flush.
		 */
		virtual Status step(char* input, size_t& input_len,
		                    char* output, size_t& output_len);
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
