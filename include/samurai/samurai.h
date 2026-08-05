/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_FIRST_INCLUDE_H
#define HAVE_SAMURAI_FIRST_INCLUDE_H

#include <samurai/defines.h>

#include <sys/types.h>
#include <assert.h>

/*
 * <stdint.h> rather than <cstdint>: the library and its consumers name uint64_t
 * and friends unqualified, and only the C header is required to put them in the
 * global namespace.
 *
 * It is included unconditionally. C++11 onwards guarantees it, so probing for
 * it - and hand-writing the fixed width types where the probe failed - can only
 * ever conflict with the real ones now.
 */
#include <stdint.h>

#include <samurai/stdc.h>
#include <samurai/debug/dbg.h>
#include <samurai/messagehandler.h>
#include <samurai/exception.h>

/*
 * File::getOwner() and File::getGroup() are declared in terms of these, and
 * <sys/types.h> only defines them on POSIX.
 */
#ifndef SAMURAI_POSIX
	typedef unsigned int uid_t;
	typedef unsigned int gid_t;
#endif


#endif // HAVE_SAMURAI_FIRST_INCLUDE_H
