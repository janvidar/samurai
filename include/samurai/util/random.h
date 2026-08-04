/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SYSTEM_RANDOM_GENERATOR_H
#define HAVE_SYSTEM_RANDOM_GENERATOR_H

#include <span>
#include <stddef.h>
#include <stdint.h>

namespace Samurai {
namespace Util {

/**
 * Generates a pseudo-random number in the range [low, high) - low inclusive,
 * high exclusive. Returns low if high <= low.
 *
 * NOTE: This is seeded from the clock and is not suitable for keys, nonces,
 * session identifiers or anything else where predictability matters. Use
 * secureRandom() for those.
 */
int pseudoRandom(int low, int high);

/**
 * Fills 'out' with cryptographically strong random bytes.
 *
 * @return false if the platform would not supply them, in which case the
 *         contents of 'out' are unspecified and must not be used. Nothing weaker
 *         is substituted: a caller that needs a key, a nonce or a token and
 *         cannot have one has to fail, because a predictable value is worse than
 *         no value - it looks like a secret without being one.
 *
 * The kernel is asked directly rather than through the TLS library, although
 * OpenSSL is a mandatory dependency and offers RAND_bytes():
 *
 * - One less thing that depends on which TLS backend was built. samurai
 *   abstracts that deliberately, and where randomness comes from should not
 *   change when the backend does.
 * - No userspace generator state, so there is nothing to reseed and nothing to
 *   get wrong across fork(): a library DRBG has to be told about one, and a
 *   child that was not told repeats its parent's stream.
 * - Failure is all or nothing rather than partial.
 *
 * Where the bytes come from:
 *
 * - getentropy(), where there is one - glibc 2.25 and newer, macOS 10.12 and
 *   newer, and the BSDs. This is the preferred path.
 * - /dev/urandom otherwise, for an older POSIX system. Deliberately not
 *   /dev/random: once the pool is seeded the two are equally strong, and
 *   /dev/random has historically blocked instead, which turns asking for a token
 *   into stalling the process.
 * - BCryptGenRandom() on Windows.
 *
 * Thread safe.
 */
[[nodiscard]] bool secureRandom(std::span<uint8_t> out);

/** As above, for a caller holding a plain buffer. */
[[nodiscard]] bool secureRandom(void* out, size_t length);

/**
 * Compares two byte ranges without leaking where they first differ.
 *
 * Ranges of different length are not equal, and that is answered immediately: a
 * length is not usually the secret. Everything else takes the same time whatever
 * the contents are.
 *
 * For checking a secret a peer supplied against one this process holds - a
 * transfer token, a password digest, a MAC. memcmp() and std::string's ==
 * return as soon as a byte differs, which tells anyone who can measure it how
 * much of a guess was right, and that turns guessing a whole secret into
 * guessing it one byte at a time.
 */
[[nodiscard]] bool constantTimeEquals(std::span<const uint8_t> a,
                                      std::span<const uint8_t> b);

/** As above, for two NUL terminated strings. A null pointer equals nothing. */
[[nodiscard]] bool constantTimeEquals(const char* a, const char* b);

}
}

#endif // HAVE_SYSTEM_RANDOM_GENERATOR_H
