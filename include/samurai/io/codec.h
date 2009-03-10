/*
 * Copyright (C) 2001-2006 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_QUICKDC_CODEC_IO_H
#define HAVE_QUICKDC_CODEC_IO_H

#include <samurai/samurai.h>

namespace Samurai {
namespace IO {

class Codec {
	public:
		virtual ~Codec();
		virtual bool exec(char* input, size_t& input_len, char* output, size_t& output_len) = 0;
};
	
}
}

#endif // HAVE_QUICKDC_CODEC_IO_H
