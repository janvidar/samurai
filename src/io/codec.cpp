/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/codec.h>

Samurai::IO::Codec::~Codec() { }

Samurai::IO::Codec::Progress Samurai::IO::Codec::step(std::span<const char> input,
                                                      std::span<char> output)
{
	std::error_code ec;
	return internal_step(input, output, ec);
}

Samurai::IO::Codec::Progress Samurai::IO::Codec::step(std::span<const char> input,
                                                      std::span<char> output,
                                                      std::error_code& ec)
{
	ec.clear();
	return internal_step(input, output, ec);
}
