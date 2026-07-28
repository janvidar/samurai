#ifndef HAVE_SAMURAI_UTIL_FORMAT_H
#define HAVE_SAMURAI_UTIL_FORMAT_H

#include <samurai/samurai.h>
#include <string>

namespace Samurai {
namespace Util {

/**
 * Render a byte count with a binary unit suffix, e.g. "   4 KB" or "1.50 MB".
 *
 * Returns the string rather than a pointer into a shared buffer: the previous
 * form wrote into a function-local static, so two calls in one expression both
 * yielded the same text, and it was not reentrant.
 */
std::string formatSize(uint64_t size);

}
}

#endif // HAVE_SAMURAI_UTIL_FORMAT_H
