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
#include <memory>
#include <errno.h>

class Bz2Private
{
	public:
		Bz2Private()
		{
			stream = std::make_unique<bz_stream>();
			stream->bzalloc = 0;
			stream->bzfree = 0;
			stream->opaque = 0;
		}

		std::unique_ptr<bz_stream> stream;

		/* Owns a codec stream: copying would release it twice. */
		Bz2Private(const Bz2Private&) = delete;
		Bz2Private& operator=(const Bz2Private&) = delete;
};

class GzPrivate
{
	public:
		GzPrivate()
		{
			stream = std::make_unique<z_stream>();
			stream->zalloc = 0;
			stream->zfree = 0;
			stream->opaque = 0;
		}

		std::unique_ptr<z_stream> stream;

		/* Owns a codec stream: copying would release it twice. */
		GzPrivate(const GzPrivate&) = delete;
		GzPrivate& operator=(const GzPrivate&) = delete;
};


/* Map a library status onto errno-space so callers get something better than
   "it failed". */
static std::error_code bz2_error(int status)
{
	switch (status) {
		case BZ_MEM_ERROR:         return Samurai::system_error(ENOMEM);
		case BZ_DATA_ERROR:
		case BZ_DATA_ERROR_MAGIC:  return Samurai::system_error(EILSEQ);
		case BZ_PARAM_ERROR:       return Samurai::system_error(EINVAL);
		case BZ_SEQUENCE_ERROR:    return Samurai::system_error(EPERM);
		case BZ_OUTBUFF_FULL:      return Samurai::system_error(ENOBUFS);
		default:                   return Samurai::system_error(EIO);
	}
}

static std::error_code gz_error(int status)
{
	switch (status) {
		case Z_MEM_ERROR:    return Samurai::system_error(ENOMEM);
		case Z_DATA_ERROR:   return Samurai::system_error(EILSEQ);
		case Z_STREAM_ERROR: return Samurai::system_error(EINVAL);
		case Z_BUF_ERROR:    return Samurai::system_error(ENOBUFS);
		default:             return Samurai::system_error(EIO);
	}
}


Samurai::IO::BZip2Compressor::BZip2Compressor()
{
	m_last_status = 0;
	d = std::make_unique<Bz2Private>();
	
	if (BZ2_bzCompressInit(d->stream.get(), 5, 0, 0) != BZ_OK)
	{
		d.reset();
	}
}

Samurai::IO::BZip2Compressor::~BZip2Compressor()
{
#ifdef DATADUMP
	if (d && d->stream)
		printf("~BZ2_Compressor, %u/%u = %.04f\n", d->stream->total_out_lo32, d->stream->total_in_lo32,  (float) d->stream->total_out_lo32 / (float)(d->stream->total_in_lo32 + 1));
#endif
	if (d && d->stream)
		BZ2_bzCompressEnd(d->stream.get());
}

bool Samurai::IO::BZip2Compressor::exec(char* input, size_t& input_len, char* output, size_t& output_len)
{
	if (!output_len || !d) return false;
	
	d->stream->avail_in = input_len;
	d->stream->next_in = input;
	d->stream->avail_out = output_len;
	d->stream->next_out = output;
	
	int action = (input_len) ? BZ_RUN : BZ_FINISH;
	int retval = BZ2_bzCompress(d->stream.get(), action);
	m_last_status = retval;

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
	m_last_status = 0;
	d = std::make_unique<Bz2Private>();
	
	if (BZ2_bzDecompressInit(d->stream.get(), 0, 0) != BZ_OK)
	{
		d.reset();
	}
}
		
Samurai::IO::BZip2Decompressor::~BZip2Decompressor()
{
#ifdef DATADUMP
	if (d && d->stream)
		printf("~BZip2Decompressor, %u/%u = %.04f\n", d->stream->total_out_lo32, d->stream->total_in_lo32,  (float)d->stream->total_out_lo32 / (float)(d->stream->total_in_lo32 + 1));
#endif
	if (d && d->stream)
		BZ2_bzDecompressEnd(d->stream.get());
}

bool Samurai::IO::BZip2Decompressor::exec(char* input, size_t& input_len, char* output, size_t& output_len)
{
	if (!output_len || !d) return false;
	
	d->stream->avail_in = input_len;
	d->stream->next_in = (char*) input;
	d->stream->avail_out = output_len;
	d->stream->next_out = (char*) output;

	int retval = BZ2_bzDecompress(d->stream.get());
	m_last_status = retval;

	if (retval == BZ_OK || retval == BZ_STREAM_END)
	{
		output_len -= d->stream->avail_out;
		input_len -= d->stream->avail_in;
		return true;
	}
	return false;
}

Samurai::IO::GzipCompressor::GzipCompressor()
{
	m_last_status = 0;
	d = std::make_unique<GzPrivate>();
	
	 // FIXME: Default compression level: 5
	if (deflateInit(d->stream.get(), 5) != Z_OK)
	{
		d.reset();
	}
}

Samurai::IO::GzipCompressor::~GzipCompressor()
{
	if (d && d->stream)
		deflateEnd(d->stream.get());
}



bool Samurai::IO::GzipCompressor::exec(char* input, size_t& input_len, char* output, size_t& output_len)
{
	if (!output_len || !d) return false;

	d->stream->avail_in = input_len;
	d->stream->next_in = (Bytef*) input;
	d->stream->avail_out = output_len;
	d->stream->next_out = (Bytef*) output;
	
	int action = (input_len) ? Z_NO_FLUSH : Z_FINISH;
	int retval = deflate(d->stream.get(), action);
	m_last_status = retval;

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
	m_last_status = 0;
	d = std::make_unique<GzPrivate>();
	if (inflateInit(d->stream.get()) != Z_OK)
	{
		d.reset();
	}
	
}

Samurai::IO::GzipDecompressor::~GzipDecompressor()
{
	if (d && d->stream)
		inflateEnd(d->stream.get());
}

bool Samurai::IO::GzipDecompressor::exec(char* input, size_t& input_len, char* output, size_t& output_len)
{
	if (!output_len || !d) return false;
	
	d->stream->avail_in = input_len;
	d->stream->next_in = (Bytef*) input;
	d->stream->avail_out = output_len;
	d->stream->next_out = (Bytef*) output;

	int retval = inflate(d->stream.get(), Z_NO_FLUSH);
	m_last_status = retval;
	
	if (retval == Z_OK || retval == Z_STREAM_END) {
		output_len -= d->stream->avail_out;
		input_len  -= d->stream->avail_in;
		return true;
	}
	return false;
}

bool Samurai::IO::BZip2Compressor::exec(char* input, size_t& input_len,
                                              char* output, size_t& output_len, std::error_code& ec)
{
	ec.clear();
	if (!d) { ec = Samurai::system_error(ENOMEM); return false; }
	if (!output_len) { ec = Samurai::system_error(ENOBUFS); return false; }

	if (exec(input, input_len, output, output_len)) return true;

	ec = bz2_error(m_last_status);
	return false;
}

bool Samurai::IO::BZip2Decompressor::exec(char* input, size_t& input_len,
                                              char* output, size_t& output_len, std::error_code& ec)
{
	ec.clear();
	if (!d) { ec = Samurai::system_error(ENOMEM); return false; }
	if (!output_len) { ec = Samurai::system_error(ENOBUFS); return false; }

	if (exec(input, input_len, output, output_len)) return true;

	ec = bz2_error(m_last_status);
	return false;
}

bool Samurai::IO::GzipCompressor::exec(char* input, size_t& input_len,
                                              char* output, size_t& output_len, std::error_code& ec)
{
	ec.clear();
	if (!d) { ec = Samurai::system_error(ENOMEM); return false; }
	if (!output_len) { ec = Samurai::system_error(ENOBUFS); return false; }

	if (exec(input, input_len, output, output_len)) return true;

	ec = gz_error(m_last_status);
	return false;
}

bool Samurai::IO::GzipDecompressor::exec(char* input, size_t& input_len,
                                              char* output, size_t& output_len, std::error_code& ec)
{
	ec.clear();
	if (!d) { ec = Samurai::system_error(ENOMEM); return false; }
	if (!output_len) { ec = Samurai::system_error(ENOBUFS); return false; }

	if (exec(input, input_len, output, output_len)) return true;

	ec = gz_error(m_last_status);
	return false;
}
