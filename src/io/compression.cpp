/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifdef DATADUMP
#include <stdio.h>
#endif

#include <bzlib.h>
#include <zlib.h>

#include <samurai/io/compression.h>

class Bz2Private
{
	public:
		Bz2Private()
		{
			stream = new bz_stream;
			stream->bzalloc = 0;
			stream->bzfree = 0;
			stream->opaque = 0;
		}
		
		bz_stream* stream;
};

class GzPrivate
{
	public:
		GzPrivate()
		{
			stream = new z_stream;
			stream->zalloc = 0;
			stream->zfree = 0;
			stream->opaque = 0;
		}
		
		z_stream* stream;
};


Samurai::IO::BZip2Compressor::BZip2Compressor()
{
	d = new Bz2Private();
	
	if (BZ2_bzCompressInit(d->stream, 5, 0, 0) != BZ_OK)
	{
		delete d; d = 0;
	}
}

Samurai::IO::BZip2Compressor::~BZip2Compressor()
{
#ifdef DATADUMP
	printf("~BZ2_Compressor, %u/%u = %.04f\n", d->stream->total_out_lo32, d->stream->total_in_lo32,  (float) d->stream->total_out_lo32 / (float)(d->stream->total_in_lo32 + 1));
#endif
	if (d && d->stream)
		BZ2_bzCompressEnd(d->stream);
	delete d;
}

bool Samurai::IO::BZip2Compressor::exec(char* input, size_t& input_len, char* output, size_t& output_len)
{
	if (!output_len || !d) return false;
	
	d->stream->avail_in = input_len;
	d->stream->next_in = input;
	d->stream->avail_out = output_len;
	d->stream->next_out = output;
	
	int action = (input_len) ? BZ_RUN : BZ_FINISH;
	int retval = BZ2_bzCompress(d->stream, action);

	if (retval == BZ_RUN_OK || retval == BZ_FINISH_OK || retval == BZ_STREAM_END)
	{
		output_len -= d->stream->avail_out;
		input_len -=  d->stream->avail_in;
		return true;
	}
	
	return false;
}

Samurai::IO::BZip2Decompressor::BZip2Decompressor()
{
	d = new Bz2Private();
	
	if (BZ2_bzDecompressInit(d->stream, 0, 0) != BZ_OK)
	{
		delete d; d = 0;
	}
}
		
Samurai::IO::BZip2Decompressor::~BZip2Decompressor()
{
#ifdef DATADUMP
	printf("~BZip2Decompressor, %u/%u = %.04f\n", d->stream->total_out_lo32, d->stream->total_in_lo32,  (float)d->stream->total_out_lo32 / (float)(d->stream->total_in_lo32 + 1));
#endif
	if (d && d->stream)
		BZ2_bzDecompressEnd(d->stream);
	delete d;
}

bool Samurai::IO::BZip2Decompressor::exec(char* input, size_t& input_len, char* output, size_t& output_len)
{
	if (!output_len || !d) return false;
	
	d->stream->avail_in = input_len;
	d->stream->next_in = (char*) input;
	d->stream->avail_out = output_len;
	d->stream->next_out = (char*) output;

	int retval = BZ2_bzDecompress(d->stream);
	
	if (retval == BZ_OK)
	{
		output_len -= d->stream->avail_out;
		input_len -= d->stream->avail_in;
		return true;
	}
	return false;
}

Samurai::IO::GzipCompressor::GzipCompressor()
{
	d = new GzPrivate();
	
	 // FIXME: Default compression level: 5
	if (deflateInit(d->stream, 5) != Z_OK)
	{
		delete d; d = 0;
	}
}

Samurai::IO::GzipCompressor::~GzipCompressor()
{
	if (d && d->stream)
		deflateEnd(d->stream);
	delete d;
}



bool Samurai::IO::GzipCompressor::exec(char* input, size_t& input_len, char* output, size_t& output_len)
{
	if (!output_len || !d) return false;

	d->stream->avail_in = input_len;
	d->stream->next_in = (Bytef*) input;
	d->stream->avail_out = output_len;
	d->stream->next_out = (Bytef*) output;
	
	int action = (input_len) ? Z_NO_FLUSH : Z_FINISH;
	int retval = deflate(d->stream, action);

	if (retval == Z_OK || retval == Z_STREAM_END)
	{
		output_len -= d->stream->avail_out;
		input_len -=  d->stream->avail_in;
		return true;
	}
	return false;
}


Samurai::IO::GzipDecompressor::GzipDecompressor()
{
	d = new GzPrivate();
	if (inflateInit(d->stream) != Z_OK)
	{
		delete d; d = 0;
	}
	
}

Samurai::IO::GzipDecompressor::~GzipDecompressor()
{
	if (d && d->stream)
		inflateEnd(d->stream);
	delete d;
}

bool Samurai::IO::GzipDecompressor::exec(char* input, size_t& input_len, char* output, size_t& output_len)
{
	if (!output_len || !d) return false;
	
	d->stream->avail_in = input_len;
	d->stream->next_in = (Bytef*) input;
	d->stream->avail_out = output_len;
	d->stream->next_out = (Bytef*) output;

	int retval = inflate(d->stream, Z_NO_FLUSH);
	
	if (retval == Z_OK || retval == Z_STREAM_END) {
		output_len -= d->stream->avail_out;
		input_len  -= d->stream->avail_in;
		return true;
	}
	return false;
}
