/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_POSIX_H
#define HAVE_SAMURAI_POSIX_H

/*
 * <unistd.h> where there is one, and on Windows the headers that carry the same
 * calls under the same names (read, write, close, access, getpid, unlink, ...),
 * so a file that only needs those does not have to know which platform it is on.
 */

#include <samurai/defines.h>

#ifdef SAMURAI_POSIX
#include <unistd.h>
#else
#include <direct.h>
#include <io.h>
#include <process.h>

#ifndef F_OK
#define F_OK 0
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef R_OK
#define R_OK 4
#endif
/* _access() has no execute bit and rejects asking for one; existence is the nearest question. */
#ifndef X_OK
#define X_OK 0
#endif
#endif

#endif // HAVE_SAMURAI_POSIX_H
