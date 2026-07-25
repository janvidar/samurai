/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */


#include <samurai/samurai.h>
#include <samurai/util/random.h>

#ifdef SAMURAI_OS_WINDOWS
#define _CRT_RAND_S
#endif

#include <stdlib.h>
#include <time.h>

#ifdef SAMURAI_POSIX
#include <sys/time.h>
#include <unistd.h>
#endif

#ifdef SAMURAI_OS_WINDOWS
#include <limits.h>
#endif

/*
 * NOTE: The generator must be seeded exactly once. Re-seeding from the clock
 * on every call - as this used to do - means repeated calls within the same
 * clock tick all return the same number, and makes the whole sequence
 * recoverable from the wall clock.
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

	/* NOTE: The division must be done in floating point. This used to read
	   (int) (double) rand() / (RAND_MAX + 1) * (high - low) + low, where the
	   cast binds tighter than the division - so it was integer division that
	   always yielded 0, and the function always returned 'low'. */
	return low + (int) (((double) rand() / ((double) RAND_MAX + 1.0)) * (high - low));
}
#endif
