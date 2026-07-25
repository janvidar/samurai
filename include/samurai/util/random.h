/*
 * Copyright (C) 2001-2006 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SYSTEM_RANDOM_GENERATOR_H
#define HAVE_SYSTEM_RANDOM_GENERATOR_H
namespace Samurai {
namespace Util {

/**
 * Generates a pseudo-random number in the range [low, high) - low inclusive,
 * high exclusive. Returns low if high <= low.
 *
 * NOTE: This is seeded from the clock and is not suitable for keys, nonces,
 * session identifiers or anything else where predictability matters.
 */
int pseudoRandom(int low, int high);

}
}

#endif // HAVE_SYSTEM_RANDOM_GENERATOR_H

