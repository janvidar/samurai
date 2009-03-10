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
#include <sys/time.h>
#include <time.h>

#ifdef SAMURAI_OS_WINDOWS
#include <limits.h>
#endif

#ifdef SAMURAI_POSIX
// NOTE: We try seeding the values from the system clock.
int Samurai::Util::pseudoRandom(int low, int high)
{
	struct timeval time;
	gettimeofday(&time, 0);
	srand48((long) (time.tv_sec + time.tv_usec));
	return  (1+(int) ((high - low) * drand48()) - 1) + low;
}
#endif

#ifdef SAMURAI_OS_WINDOWS
int Samurai::Util::pseudoRandom(int low, int high)
{
	srand(time(0));
	return (int) (double) rand() / (RAND_MAX + 1) * (high - low) + low;
}
#endif

