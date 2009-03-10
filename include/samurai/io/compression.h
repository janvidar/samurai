/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_QUICKDC_COMPRESSION_IO_H
#define HAVE_QUICKDC_COMPRESSION_IO_H

#include <samurai/io/codec.h>

class Bz2Private;
class GzPrivate;

namespace Samurai {
namespace IO {

class BZip2Compressor : public Samurai::IO::Codec {
	public:
		BZip2Compressor();
		virtual ~BZip2Compressor();
		bool exec(char* input, size_t& input_len, char* output, size_t& output_len);
	protected:
		Bz2Private* d;
};

class BZip2Decompressor : public Samurai::IO::Codec {
	public:
		BZip2Decompressor();
		virtual ~BZip2Decompressor();
		bool exec(char* input, size_t& input_len, char* output, size_t& output_len);
	protected:
		Bz2Private* d;
};

class GzipCompressor : public Samurai::IO::Codec {
	public:
		GzipCompressor();
		virtual ~GzipCompressor();
		bool exec(char* input, size_t& input_len, char* output, size_t& output_len);
	protected:
		GzPrivate* d;
};

class GzipDecompressor : public Samurai::IO::Codec {
	public:
		GzipDecompressor();
		virtual ~GzipDecompressor();
		bool exec(char* input, size_t& input_len, char* output, size_t& output_len);
	protected:
		GzPrivate* d;
};

}
}

#endif // HAVE_QUICKDC_COMPRESSION_IO_H
