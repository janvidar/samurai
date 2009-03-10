/*
 * Copyright (C) 2001-2006 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SYSTEM_RANDOM_GENERATOR_H
#define HAVE_SYSTEM_RANDOM_GENERATOR_H
namespace Samurai {
namespace Util {

/**
 * Generates a pseudo-random number between low and high.
 */
int pseudoRandom(int low, int high);

}
}

#endif // HAVE_SYSTEM_RANDOM_GENERATOR_H

