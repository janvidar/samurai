/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_CODEC_IO_H
#define HAVE_SAMURAI_CODEC_IO_H

#include <samurai/samurai.h>
#include <samurai/error.h>

#include <cstddef>
#include <span>

namespace Samurai {
namespace IO {

class Codec {
	public:
		enum class Status
		{
			Ok,        /**< progress was made; call again */
			StreamEnd, /**< the stream is complete; do not call again */
			Error      /**< failed; the error_code overload says why */
		};

		/**
		 * What one step did.
		 *
		 * The two counts are separate from the buffers they refer to, so
		 * neither is a length that means one thing going in and another
		 * coming out.
		 */
		struct Progress
		{
			Status status = Status::Error;
			size_t consumed = 0; /**< bytes taken from the input */
			size_t produced = 0; /**< bytes written to the output */
		};

		virtual ~Codec();

		/**
		 * Transform input into output.
		 *
		 * An empty input asks the codec to flush. A flush too large for one
		 * output buffer takes several calls and consumes nothing on any of
		 * them, so a loop that stops as soon as nothing moved truncates the
		 * stream. Stop on the status instead:
		 *
		 *     while (true) {
		 *         const Codec::Progress p = codec.step(input, output);
		 *         if (p.status == Codec::Status::Error) return false;
		 *         sink(output.first(p.produced));
		 *         input = input.subspan(p.consumed);
		 *         if (p.status == Codec::Status::StreamEnd) return true;
		 *     }
		 */
		Progress step(std::span<const char> input, std::span<char> output);

		/** As step(), but saying why on Status::Error. */
		Progress step(std::span<const char> input, std::span<char> output,
		              std::error_code& ec);

	protected:
		/**
		 * The one function a codec implements. The overloads above are not
		 * virtual, so overriding this cannot hide either of them.
		 */
		virtual Progress internal_step(std::span<const char> input,
		                               std::span<char> output,
		                               std::error_code& ec) = 0;
};

}
}

#endif // HAVE_SAMURAI_CODEC_IO_H
