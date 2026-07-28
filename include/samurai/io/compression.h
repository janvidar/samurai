/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_COMPRESSION_IO_H
#define HAVE_SAMURAI_COMPRESSION_IO_H

#include <samurai/io/codec.h>

class Bz2Private;
class GzPrivate;

namespace Samurai {
namespace IO {

class BZip2Compressor final : public Samurai::IO::Codec {
	public:
		BZip2Compressor();
		~BZip2Compressor() override;
		bool exec(char* input, size_t& input_len, char* output, size_t& output_len) override;
		bool exec(char* input, size_t& input_len, char* output, size_t& output_len, std::error_code& ec) override;

		/* Owns a zlib/bzip2 stream: copying would free it twice. */
		BZip2Compressor(const BZip2Compressor&) = delete;
		BZip2Compressor& operator=(const BZip2Compressor&) = delete;

	protected:
		Bz2Private* d;
		int m_last_status;

};

class BZip2Decompressor final : public Samurai::IO::Codec {
	public:
		BZip2Decompressor();
		~BZip2Decompressor() override;
		bool exec(char* input, size_t& input_len, char* output, size_t& output_len) override;
		bool exec(char* input, size_t& input_len, char* output, size_t& output_len, std::error_code& ec) override;

		/* Owns a zlib/bzip2 stream: copying would free it twice. */
		BZip2Decompressor(const BZip2Decompressor&) = delete;
		BZip2Decompressor& operator=(const BZip2Decompressor&) = delete;

	protected:
		Bz2Private* d;
		int m_last_status;

};

class GzipCompressor final : public Samurai::IO::Codec {
	public:
		GzipCompressor();
		~GzipCompressor() override;
		bool exec(char* input, size_t& input_len, char* output, size_t& output_len) override;
		bool exec(char* input, size_t& input_len, char* output, size_t& output_len, std::error_code& ec) override;

		/* Owns a zlib/bzip2 stream: copying would free it twice. */
		GzipCompressor(const GzipCompressor&) = delete;
		GzipCompressor& operator=(const GzipCompressor&) = delete;

	protected:
		GzPrivate* d;
		int m_last_status;

};

class GzipDecompressor final : public Samurai::IO::Codec {
	public:
		GzipDecompressor();
		~GzipDecompressor() override;
		bool exec(char* input, size_t& input_len, char* output, size_t& output_len) override;
		bool exec(char* input, size_t& input_len, char* output, size_t& output_len, std::error_code& ec) override;

		/* Owns a zlib/bzip2 stream: copying would free it twice. */
		GzipDecompressor(const GzipDecompressor&) = delete;
		GzipDecompressor& operator=(const GzipDecompressor&) = delete;

	protected:
		GzPrivate* d;
		int m_last_status;

};

}
}

#endif // HAVE_SAMURAI_COMPRESSION_IO_H
