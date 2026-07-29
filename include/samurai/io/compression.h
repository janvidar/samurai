/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_COMPRESSION_IO_H
#define HAVE_SAMURAI_COMPRESSION_IO_H

#include <samurai/io/codec.h>
#include <memory>

class Bz2Private;
class GzPrivate;

namespace Samurai {
namespace IO {

class BZip2Compressor final : public Samurai::IO::Codec {
	public:
		BZip2Compressor();
		~BZip2Compressor() override;
		/* Owns a zlib/bzip2 stream: copying would free it twice. */
		BZip2Compressor(const BZip2Compressor&) = delete;
		BZip2Compressor& operator=(const BZip2Compressor&) = delete;

	protected:
		Progress internal_step(std::span<const char> input, std::span<char> output,
		                       std::error_code& ec) override;

		std::unique_ptr<Bz2Private> d;

};

class BZip2Decompressor final : public Samurai::IO::Codec {
	public:
		BZip2Decompressor();
		~BZip2Decompressor() override;
		/* Owns a zlib/bzip2 stream: copying would free it twice. */
		BZip2Decompressor(const BZip2Decompressor&) = delete;
		BZip2Decompressor& operator=(const BZip2Decompressor&) = delete;

	protected:
		Progress internal_step(std::span<const char> input, std::span<char> output,
		                       std::error_code& ec) override;

		std::unique_ptr<Bz2Private> d;

};

class GzipCompressor final : public Samurai::IO::Codec {
	public:
		GzipCompressor();
		~GzipCompressor() override;
		/* Owns a zlib/bzip2 stream: copying would free it twice. */
		GzipCompressor(const GzipCompressor&) = delete;
		GzipCompressor& operator=(const GzipCompressor&) = delete;

	protected:
		Progress internal_step(std::span<const char> input, std::span<char> output,
		                       std::error_code& ec) override;

		std::unique_ptr<GzPrivate> d;

};

class GzipDecompressor final : public Samurai::IO::Codec {
	public:
		GzipDecompressor();
		~GzipDecompressor() override;
		/* Owns a zlib/bzip2 stream: copying would free it twice. */
		GzipDecompressor(const GzipDecompressor&) = delete;
		GzipDecompressor& operator=(const GzipDecompressor&) = delete;

	protected:
		Progress internal_step(std::span<const char> input, std::span<char> output,
		                       std::error_code& ec) override;

		std::unique_ptr<GzPrivate> d;

};

}
}

#endif // HAVE_SAMURAI_COMPRESSION_IO_H
