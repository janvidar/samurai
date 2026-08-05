/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/compression.h>
#include <samurai/io/codec.h>
#include <span>
#include <string>
#include <vector>
#include <string.h>

/*
 * Codec::step() reports what it took and what it made separately from the
 * buffers it was given, so neither is a length meaning one thing on the way in
 * and another on the way out. An empty input asks the codec to finish.
 *
 * The helpers live at file scope because EXO_TEST takes two macro arguments and
 * a braced initialiser inside a case would be split on its commas.
 */

/**
 * Run a codec to completion.
 *
 * A flush too large for one output buffer needs several calls that each consume
 * nothing, so "stop when nothing was consumed" truncates. Status::StreamEnd is
 * what says when to stop.
 */
static bool codec_run(Samurai::IO::Codec& codec, const std::string& in, std::string& out,
                      size_t chunk_size = 64)
{
	std::vector<char> chunk(chunk_size);
	std::span<const char> input(in);
	out.clear();

	for (int spins = 0; spins < 100000; spins++)
	{
		const Samurai::IO::Codec::Progress p = codec.step(input, chunk);

		if (p.status == Samurai::IO::Codec::Status::Error) return false;

		if (p.produced) out.append(chunk.data(), p.produced);
		input = input.subspan(p.consumed);

		if (p.status == Samurai::IO::Codec::Status::StreamEnd) return true;
		if (!p.consumed && !p.produced) return true;
	}
	return false;
}

static const std::string compressible =
	"the quick brown fox jumps over the lazy dog, and does so repeatedly, "
	"repeatedly, repeatedly, repeatedly, repeatedly, repeatedly, repeatedly.";

/* ------------------------------------------------------------------------- */
/* gzip                                                                       */
/* ------------------------------------------------------------------------- */

EXO_TEST(gzip_round_trip,
{
	Samurai::IO::GzipCompressor deflater;
	std::string packed;
	if (!codec_run(deflater, compressible, packed)) return false;
	if (packed.empty()) return false;

	Samurai::IO::GzipDecompressor inflater;
	std::string unpacked;
	if (!codec_run(inflater, packed, unpacked)) return false;

	return unpacked == compressible;
});

EXO_TEST(gzip_actually_compresses,
{
	Samurai::IO::GzipCompressor deflater;
	std::string packed;
	if (!codec_run(deflater, compressible, packed)) return false;
	return packed.size() < compressible.size();
});

EXO_TEST(gzip_round_trip_empty_input,
{
	Samurai::IO::GzipCompressor deflater;
	std::string packed;
	if (!codec_run(deflater, std::string(), packed)) return false;

	Samurai::IO::GzipDecompressor inflater;
	std::string unpacked;
	if (!codec_run(inflater, packed, unpacked)) return false;

	return unpacked.empty();
});

/* A tiny output buffer must still converge, just over more iterations. */
EXO_TEST(gzip_round_trip_tiny_output_buffer,
{
	Samurai::IO::GzipCompressor deflater;
	std::string packed;
	if (!codec_run(deflater, compressible, packed, 1)) return false;

	Samurai::IO::GzipDecompressor inflater;
	std::string unpacked;
	if (!codec_run(inflater, packed, unpacked, 1)) return false;

	return unpacked == compressible;
});

EXO_TEST(gzip_binary_safe,
{
	std::string binary;
	for (int n = 0; n < 512; n++) binary.push_back((char) (n & 0xff));

	Samurai::IO::GzipCompressor deflater;
	std::string packed;
	if (!codec_run(deflater, binary, packed)) return false;

	Samurai::IO::GzipDecompressor inflater;
	std::string unpacked;
	if (!codec_run(inflater, packed, unpacked)) return false;

	return unpacked == binary;
});

/* ------------------------------------------------------------------------- */
/* bzip2                                                                      */
/* ------------------------------------------------------------------------- */

EXO_TEST(bzip2_round_trip,
{
	Samurai::IO::BZip2Compressor packer;
	std::string packed;
	if (!codec_run(packer, compressible, packed)) return false;
	if (packed.empty()) return false;

	Samurai::IO::BZip2Decompressor unpacker;
	std::string unpacked;
	if (!codec_run(unpacker, packed, unpacked)) return false;

	return unpacked == compressible;
});

EXO_TEST(bzip2_round_trip_tiny_output_buffer,
{
	Samurai::IO::BZip2Compressor packer;
	std::string packed;
	if (!codec_run(packer, compressible, packed, 1)) return false;

	Samurai::IO::BZip2Decompressor unpacker;
	std::string unpacked;
	if (!codec_run(unpacker, packed, unpacked, 1)) return false;

	return unpacked == compressible;
});

EXO_TEST(bzip2_binary_safe,
{
	std::string binary;
	for (int n = 0; n < 512; n++) binary.push_back((char) ((n * 7) & 0xff));

	Samurai::IO::BZip2Compressor packer;
	std::string packed;
	if (!codec_run(packer, binary, packed)) return false;

	Samurai::IO::BZip2Decompressor unpacker;
	std::string unpacked;
	if (!codec_run(unpacker, packed, unpacked)) return false;

	return unpacked == binary;
});

/*
 * A flush too large for one output buffer takes several calls, and every one of
 * them consumes nothing. A loop that stops at the first keeps only what fitted.
 *
 * This is not hypothetical: it is how the share manager wrote its compressed
 * file list. The payload here is deliberately poor at compressing, because that
 * is what makes the final block bigger than the buffer - a file list gets there
 * through one random TTH per file.
 */
EXO_TEST(codec_a_flush_spanning_several_buffers_completes,
{
	std::string noise;
	unsigned seed = 12345;
	for (size_t n = 0; n < 200000; n++)
	{
		seed = seed * 1103515245u + 12345u;
		noise += (char) ((seed >> 16) & 0xff);
	}

	Samurai::IO::BZip2Compressor deflater;
	std::string packed;
	if (!codec_run(deflater, noise, packed, 4096)) return false;

	/* Several buffers' worth, or the case is not testing what it claims. */
	if (packed.size() < 4 * 4096) return false;

	Samurai::IO::BZip2Decompressor inflater;
	std::string unpacked;
	if (!codec_run(inflater, packed, unpacked, 4096)) return false;

	return unpacked == noise;
});

/* ------------------------------------------------------------------------- */
/* Failure reporting                                                          */
/* ------------------------------------------------------------------------- */

/* Zero output room cannot make progress, and the error_code overload says so
 * rather than just returning false. */
EXO_TEST(codec_no_output_room_reports_enobufs,
{
	Samurai::IO::GzipCompressor deflater;
	std::error_code ec;

	const Samurai::IO::Codec::Progress p = deflater.step(compressible, {}, ec);
	return p.status == Samurai::IO::Codec::Status::Error
		&& ec == std::errc::no_buffer_space;
});

/* The overload without an error_code reports the same failure. */
EXO_TEST(codec_both_overloads_agree,
{
	Samurai::IO::GzipCompressor deflater;

	const Samurai::IO::Codec::Progress p = deflater.step(compressible, {});
	return p.status == Samurai::IO::Codec::Status::Error;
});

/* Nothing moved on a failed step. */
EXO_TEST(codec_a_failed_step_moved_nothing,
{
	Samurai::IO::GzipCompressor deflater;

	const Samurai::IO::Codec::Progress p = deflater.step(compressible, {});
	return p.consumed == 0 && p.produced == 0;
});

/* consumed and produced are counts in their own right, not what is left over. */
EXO_TEST(codec_reports_what_it_took_and_made,
{
	Samurai::IO::GzipCompressor deflater;
	char out[512];

	const Samurai::IO::Codec::Progress p = deflater.step(compressible, out);
	return p.status == Samurai::IO::Codec::Status::Ok
		&& p.consumed == compressible.size()
		&& p.produced < sizeof(out);
});

/* Decompressing something that is not a compressed stream has to fail rather
 * than loop or return success. */
EXO_TEST(gzip_rejects_garbage,
{
	Samurai::IO::GzipDecompressor inflater;
	std::string unpacked;
	return !codec_run(inflater, std::string("this is not a deflate stream"), unpacked);
});

EXO_TEST(bzip2_rejects_garbage,
{
	Samurai::IO::BZip2Decompressor unpacker;
	std::string unpacked;
	return !codec_run(unpacker, std::string("this is not a bzip2 stream"), unpacked);
});
