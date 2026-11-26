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

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

/* Windows paths are not bounded by MAX_PATH once long paths are on; this is what a buffer holds. */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include <stdlib.h>
#include <string.h>

inline int setenv(const char* name, const char* value, int overwrite)
{
	if (!overwrite && getenv(name)) return 0;
	return _putenv_s(name, value) == 0 ? 0 : -1;
}

inline int unsetenv(const char* name)
{
	return _putenv_s(name, "") == 0 ? 0 : -1;
}

/* 'templ' ends in XXXXXX, as on POSIX; made unique, created, and returned, or null. */
inline char* mkdtemp(char* templ)
{
	if (_mktemp_s(templ, strlen(templ) + 1) != 0) return nullptr;
	return _mkdir(templ) == 0 ? templ : nullptr;
}
#endif

#endif // HAVE_SAMURAI_POSIX_H
