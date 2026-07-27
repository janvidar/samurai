/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/codec.h>
#include <errno.h>

Samurai::IO::Codec::~Codec() { }

bool Samurai::IO::Codec::exec(char* input, size_t& input_len,
                              char* output, size_t& output_len, std::error_code& ec)
{
	ec.clear();
	if (exec(input, input_len, output, output_len)) return true;
	ec = Samurai::system_error(EIO);
	return false;
}
