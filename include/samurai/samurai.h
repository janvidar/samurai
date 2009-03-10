/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_FIRST_INCLUDE_H
#define HAVE_SAMURAI_FIRST_INCLUDE_H

#include <samurai/defines.h>

#include <sys/types.h>
#include <assert.h>

#if defined(SAMURAI_OS_LINUX) || defined(SAMURAI_OS_MACOSX)
#define HAVE_STDINT_H
#define HAVE_STDINT_H
#endif

#ifndef MIN
#define MIN(a,b) a < b ? a : b
#endif

#ifndef MAX
#define MAX(a,b) a > b ? a : b
#endif

/* Misc crap */
#if defined(SAMURAI_OS_SOLARIS)
#include <unistd.h>
#define HAVE_INTTYPES_H
typedef unsigned long long uint64_t;
typedef long long int64_t;
#endif

#ifdef SAMURAI_OS_WINDOWS
	typedef __int64 int64_t;
	typedef unsigned __int64 uint64_t;
#endif

#if defined(HAVE_STDINT_H)
#include <stdint.h>
#elif defined(HAVE_INTTYPES_H)
#include <inttypes.h>
#endif

#include <samurai/stdc.h>
#include <samurai/debug/dbg.h>
#include <samurai/messagehandler.h>
#include <samurai/exception.h>

#ifndef SAMURAI_POSIX
	typedef unsigned int uid_t;
	typedef unsigned int gid_t;
#endif


#endif // HAVE_SAMURAI_FIRST_INCLUDE_H
