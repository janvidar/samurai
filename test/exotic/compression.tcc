/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/compression.h>
#include <samurai/io/codec.h>
#include <string>
#include <vector>
#include <string.h>

/*
 * Codec::exec() uses each length as an in/out parameter: on the way in it is
 * how much is available (input) or how much room there is (output), and on the
 * way out it is how much was consumed or produced. An empty input asks the
 * codec to finish.
 *
 * The helpers live at file scope because EXO_TEST takes two macro arguments and
 * a braced initialiser inside a case would be split on its commas.
 */

/**
 * Run a codec to completion through step().
 *
 * Driving it through exec() instead cannot be done correctly: a flush into a
 * small output buffer needs several calls that each consume nothing, so "stop
 * when nothing was consumed" truncates, and "stop when nothing was consumed and
 * nothing produced" calls the codec once more after it has finished - which
 * bzip2 rejects. Status::StreamEnd is what says when to stop.
 */
static bool codec_run(Samurai::IO::Codec& codec, const std::string& in, std::string& out,
                      size_t chunk_size = 64)
{
	std::vector<char> chunk(chunk_size);
	std::string input = in;
	size_t offset = 0;
	out.clear();

	for (int spins = 0; spins < 100000; spins++)
	{
		size_t consumed = input.size() - offset;
		size_t produced = chunk.size();

		char* at = input.empty() ? nullptr : &input[offset];
		const auto status = codec.step(at, consumed, chunk.data(), produced);

		if (status == Samurai::IO::Codec::Status::Error) return false;

		if (produced) out.append(chunk.data(), produced);
		offset += consumed;

		if (status == Samurai::IO::Codec::Status::StreamEnd) return true;
		if (!consumed && !produced) return true;
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

/* ------------------------------------------------------------------------- */
/* Failure reporting                                                          */
/* ------------------------------------------------------------------------- */

/* Zero output room cannot make progress, and the error_code overload says so
 * rather than just returning false. */
EXO_TEST(codec_no_output_room_reports_enobufs,
{
	Samurai::IO::GzipCompressor deflater;
	std::string in = compressible;
	size_t consumed = in.size();
	size_t produced = 0;
	std::error_code ec;

	const bool ok = deflater.exec(&in[0], consumed, nullptr, produced, ec);
	return !ok && ec == std::errc::no_buffer_space;
});

EXO_TEST(codec_bool_overload_agrees_with_error_code,
{
	Samurai::IO::GzipCompressor deflater;
	std::string in = compressible;
	size_t consumed = in.size();
	size_t produced = 0;

	return !deflater.exec(&in[0], consumed, nullptr, produced);
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
