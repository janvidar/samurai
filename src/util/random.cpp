/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */


#include <samurai/samurai.h>
#include <samurai/util/random.h>

#ifdef SAMURAI_OS_WINDOWS
#define _CRT_RAND_S
#endif

/*
 * Which interface supplies the strong bytes. Decided here rather than probed by
 * the build, as everything else in this tree keys off <samurai/defines.h>.
 *
 * getentropy() arrived at different times: glibc 2.25, macOS 10.12, OpenBSD 5.6,
 * FreeBSD 12, NetBSD 10. Anything older takes the /dev/urandom path, which every
 * POSIX system this library claims to build on has.
 */
#if defined(SAMURAI_RANDOM_GETENTROPY) || defined(SAMURAI_RANDOM_DEV_URANDOM) \
	|| defined(SAMURAI_RANDOM_BCRYPT)
/*
 * Already chosen for us. Left available so the fallback can be built and tested
 * on a machine that would otherwise never compile it, and so a platform where
 * the preferred interface exists but is known broken can be told to skip it.
 */
#elif defined(SAMURAI_OS_WINDOWS)
#  define SAMURAI_RANDOM_BCRYPT
#elif defined(__APPLE__)
#  include <Availability.h>
#  if defined(__MAC_OS_X_VERSION_MIN_REQUIRED) && __MAC_OS_X_VERSION_MIN_REQUIRED >= 101200
#    define SAMURAI_RANDOM_GETENTROPY
#  endif
#elif defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__)
#  define SAMURAI_RANDOM_GETENTROPY
#elif defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 25))
#  define SAMURAI_RANDOM_GETENTROPY
#endif

#if !defined(SAMURAI_RANDOM_GETENTROPY) && !defined(SAMURAI_RANDOM_BCRYPT) \
	&& !defined(SAMURAI_RANDOM_DEV_URANDOM)
#  define SAMURAI_RANDOM_DEV_URANDOM
#endif

#ifdef SAMURAI_RANDOM_GETENTROPY
#  if defined(__OpenBSD__)
#    include <unistd.h>
#  else
#    include <sys/random.h>
#  endif
#endif

#ifdef SAMURAI_RANDOM_DEV_URANDOM
#  include <errno.h>
#  include <fcntl.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

#ifdef SAMURAI_RANDOM_BCRYPT
#  include <windows.h>
#  include <bcrypt.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef SAMURAI_POSIX
#include <sys/time.h>
#include <unistd.h>
#endif

#ifdef SAMURAI_OS_WINDOWS
#include <limits.h>
#endif

/*
 * NOTE: The generator must be seeded exactly once. Re-seeding from the clock on
 * every call would mean repeated calls within the same clock tick all return the
 * same number, and would make the whole sequence recoverable from the wall
 * clock.
 *
 * The C++11 rule for function-local statics makes the seeding below run once
 * even if several threads call in at the same time. The generator itself is
 * still shared global state, so results are not reproducible per-thread.
 */

#ifdef SAMURAI_POSIX
static bool seedRandom()
{
	struct timeval time;
	gettimeofday(&time, 0);
	srand48(((long) time.tv_sec) ^ (((long) time.tv_usec) << 16) ^ ((long) getpid()));
	return true;
}

int Samurai::Util::pseudoRandom(int low, int high)
{
	static const bool seeded = seedRandom();
	(void) seeded;

	if (high <= low) return low;

	return low + (int) ((double) (high - low) * drand48());
}
#endif

#ifdef SAMURAI_OS_WINDOWS
static bool seedRandom()
{
	srand((unsigned int) time(0));
	return true;
}

int Samurai::Util::pseudoRandom(int low, int high)
{
	static const bool seeded = seedRandom();
	(void) seeded;

	if (high <= low) return low;

	/* NOTE: the division must be done in floating point - a cast binds tighter
	   than '/', so the parentheses are what keep this out of integer
	   division. */
	return low + (int) (((double) rand() / ((double) RAND_MAX + 1.0)) * (high - low));
}
#endif


/* ------------------------------------------------------------------------- */
/* Cryptographically strong bytes                                            */
/* ------------------------------------------------------------------------- */

#ifdef SAMURAI_RANDOM_GETENTROPY
bool Samurai::Util::secureRandom(std::span<uint8_t> out)
{
	/* getentropy() refuses more than 256 bytes at a time, so a longer request is
	   made in pieces. Each one either fills its piece or fails outright. */
	size_t done = 0;
	while (done < out.size())
	{
		const size_t chunk = (out.size() - done > 256) ? 256 : out.size() - done;
		if (getentropy(out.data() + done, chunk) != 0) return false;
		done += chunk;
	}
	return true;
}
#endif

#ifdef SAMURAI_RANDOM_DEV_URANDOM
bool Samurai::Util::secureRandom(std::span<uint8_t> out)
{
	/* Opened per call rather than kept: a descriptor held across fork() is
	   shared with the child, and one held at all can be closed by unrelated code
	   and then reopened as something else entirely. */
	const int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0) return false;

	size_t done = 0;
	while (done < out.size())
	{
		const ssize_t got = read(fd, out.data() + done, out.size() - done);
		if (got > 0) { done += (size_t) got; continue; }

		/* A signal is not a failure of the source; anything else is, and so is
		   end of file, which this device should never report. */
		if (got < 0 && errno == EINTR) continue;

		close(fd);
		return false;
	}

	close(fd);
	return true;
}
#endif

#ifdef SAMURAI_RANDOM_BCRYPT
bool Samurai::Util::secureRandom(std::span<uint8_t> out)
{
	/* USE_SYSTEM_PREFERRED_RNG means no algorithm handle has to be opened and
	   closed around every call. */
	const NTSTATUS status = BCryptGenRandom(nullptr, (PUCHAR) out.data(),
		(ULONG) out.size(), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
	return status >= 0;
}
#endif

bool Samurai::Util::secureRandom(void* out, size_t length)
{
	if (!out && length) return false;
	return secureRandom(std::span<uint8_t>(static_cast<uint8_t*>(out), length));
}


bool Samurai::Util::constantTimeEquals(std::span<const uint8_t> a,
                                       std::span<const uint8_t> b)
{
	if (a.size() != b.size()) return false;

	/*
	 * Every byte is looked at, and the result is only inspected at the end.
	 * 'volatile' is what stops the optimiser from noticing that a non-zero
	 * accumulator can never become zero again and leaving the loop early, which
	 * would put the timing signal straight back.
	 */
	volatile uint8_t difference = 0;
	for (size_t n = 0; n < a.size(); n++)
		difference = static_cast<uint8_t>(difference | (a[n] ^ b[n]));

	return difference == 0;
}


bool Samurai::Util::constantTimeEquals(const char* a, const char* b)
{
	if (!a || !b) return false;

	const size_t la = strlen(a);
	const size_t lb = strlen(b);

	return constantTimeEquals(
		std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(a), la),
		std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(b), lb));
}
