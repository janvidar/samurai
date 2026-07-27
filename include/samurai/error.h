/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_ERROR_H
#define HAVE_SAMURAI_ERROR_H

#include <system_error>

namespace Samurai {

/**
 * Wrap an errno value as a std::error_code.
 *
 * The library reports failures through a std::error_code out-parameter rather
 * than by returning a bare bool and logging: the caller decides what to do
 * about ENOENT versus EACCES, and cannot do that from a false.
 */
inline std::error_code system_error(int errnum)
{
	return std::error_code(errnum, std::system_category());
}

/**
 * Clear an out-parameter on the success path.
 */
inline void clear_error(std::error_code& ec)
{
	ec.clear();
}

namespace IO {

/**
 * Outcome of a read() on a stream that can end.
 *
 * NOTE: this exists because Socket::read() used to answer would-block, clean
 * end-of-stream and a hard error all with 0, which no caller could tell apart.
 */
enum ReadResult
{
	ReadOk,        /**< bytes were transferred */
	ReadWouldBlock,/**< nothing available right now; try again on the next event */
	ReadEndOfFile, /**< the peer closed; no more data will arrive */
	ReadError      /**< failed; the error_code says why */
};

}
}

#endif // HAVE_SAMURAI_ERROR_H
