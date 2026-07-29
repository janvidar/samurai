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

Samurai::IO::Codec::Progress Samurai::IO::BZip2Compressor::internal_step(
	std::span<const char> input, std::span<char> output, std::error_code& ec)
{
	Progress progress;

	if (!d) { ec = Samurai::system_error(ENOMEM); return progress; }
	if (output.empty()) { ec = Samurai::system_error(ENOBUFS); return progress; }

	/* bzlib takes a mutable pointer, but only reads through it. */
	d->stream->next_in = const_cast<char*>(input.data());
	d->stream->avail_in = (unsigned int) input.size();
	d->stream->next_out = output.data();
	d->stream->avail_out = (unsigned int) output.size();

	/* No input left to give is what asks it to finish. */
	const int retval = BZ2_bzCompress(d->stream.get(), input.empty() ? BZ_FINISH : BZ_RUN);

	if (retval != BZ_RUN_OK && retval != BZ_FINISH_OK && retval != BZ_STREAM_END)
	{
		ec = bz2_error(retval);
		return progress;
	}

	progress.consumed = input.size() - d->stream->avail_in;
	progress.produced = output.size() - d->stream->avail_out;
	progress.status = (retval == BZ_STREAM_END) ? Status::StreamEnd : Status::Ok;
	return progress;
}

Samurai::IO::BZip2Decompressor::BZip2Decompressor()
{
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

Samurai::IO::Codec::Progress Samurai::IO::BZip2Decompressor::internal_step(
	std::span<const char> input, std::span<char> output, std::error_code& ec)
{
	Progress progress;

	if (!d) { ec = Samurai::system_error(ENOMEM); return progress; }
	if (output.empty()) { ec = Samurai::system_error(ENOBUFS); return progress; }

	d->stream->next_in = const_cast<char*>(input.data());
	d->stream->avail_in = (unsigned int) input.size();
	d->stream->next_out = output.data();
	d->stream->avail_out = (unsigned int) output.size();

	const int retval = BZ2_bzDecompress(d->stream.get());

	if (retval != BZ_OK && retval != BZ_STREAM_END)
	{
		ec = bz2_error(retval);
		return progress;
	}

	progress.consumed = input.size() - d->stream->avail_in;
	progress.produced = output.size() - d->stream->avail_out;
	progress.status = (retval == BZ_STREAM_END) ? Status::StreamEnd : Status::Ok;
	return progress;
}

Samurai::IO::GzipCompressor::GzipCompressor()
{
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



Samurai::IO::Codec::Progress Samurai::IO::GzipCompressor::internal_step(
	std::span<const char> input, std::span<char> output, std::error_code& ec)
{
	Progress progress;

	if (!d) { ec = Samurai::system_error(ENOMEM); return progress; }
	if (output.empty()) { ec = Samurai::system_error(ENOBUFS); return progress; }

	/* zlib takes a mutable pointer, but only reads through it. */
	d->stream->next_in = (Bytef*) const_cast<char*>(input.data());
	d->stream->avail_in = (uInt) input.size();
	d->stream->next_out = (Bytef*) output.data();
	d->stream->avail_out = (uInt) output.size();

	/* No input left to give is what asks it to finish. */
	const int retval = deflate(d->stream.get(), input.empty() ? Z_FINISH : Z_NO_FLUSH);

	if (retval != Z_OK && retval != Z_STREAM_END && retval != Z_BUF_ERROR)
	{
		ec = gz_error(retval);
		return progress;
	}

	progress.consumed = input.size() - d->stream->avail_in;
	progress.produced = output.size() - d->stream->avail_out;
	progress.status = (retval == Z_STREAM_END) ? Status::StreamEnd : Status::Ok;
	return progress;
}

Samurai::IO::GzipDecompressor::GzipDecompressor()
{
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

Samurai::IO::Codec::Progress Samurai::IO::GzipDecompressor::internal_step(
	std::span<const char> input, std::span<char> output, std::error_code& ec)
{
	Progress progress;

	if (!d) { ec = Samurai::system_error(ENOMEM); return progress; }
	if (output.empty()) { ec = Samurai::system_error(ENOBUFS); return progress; }

	d->stream->next_in = (Bytef*) const_cast<char*>(input.data());
	d->stream->avail_in = (uInt) input.size();
	d->stream->next_out = (Bytef*) output.data();
	d->stream->avail_out = (uInt) output.size();

	const int retval = inflate(d->stream.get(), Z_NO_FLUSH);

	if (retval != Z_OK && retval != Z_STREAM_END && retval != Z_BUF_ERROR)
	{
		ec = gz_error(retval);
		return progress;
	}

	progress.consumed = input.size() - d->stream->avail_in;
	progress.produced = output.size() - d->stream->avail_out;
	progress.status = (retval == Z_STREAM_END) ? Status::StreamEnd : Status::Ok;
	return progress;
}
