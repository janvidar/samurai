/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_DEBUG_DBG_H
#define HAVE_SAMURAI_DEBUG_DBG_H

#ifndef BUILD
#define BUILD "internal"
#endif

/*
 * NOTE: these are included for both configurations on purpose. Around eighteen
 * translation units reach memcpy()/memset()/strcmp() through this header, so
 * the set of headers it pulls in must not change with the build type.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(DEBUG)

#if !defined(QDBG)
#define QDBG(format, ...)  do { samurai_debug(__PRETTY_FUNCTION__, __FILE__, __LINE__, format, ## __VA_ARGS__); } while(0)
#endif

#if !defined(QERR)
#define QERR(format, ...)  do { samurai_error(__PRETTY_FUNCTION__, __FILE__, __LINE__, format, ## __VA_ARGS__); } while(0)
#endif

#if !defined(QNET)
#define QNET(format, ...)  do { samurai_net(__PRETTY_FUNCTION__, __FILE__, __LINE__, format, ## __VA_ARGS__); } while(0)
#endif

#if !defined(QSEARCH)
#define QSEARCH(format, ...)  do { samurai_search(__PRETTY_FUNCTION__, __FILE__, __LINE__, format, ## __VA_ARGS__); } while(0)
#endif

#if !defined(QHUB)
#define QHUB(format, ...)  do { samurai_hub(__PRETTY_FUNCTION__, __FILE__, __LINE__, format, ## __VA_ARGS__); } while(0)
#endif

#if !defined(QDBG_INIT)
#define QDBG_INIT do { samurai_debug_init(); } while(0)
#endif

#if !defined(QDBG_FINI)
#define QDBG_FINI do { samurai_debug_fini(); } while(0)
#endif

void samurai_debug_init();
void samurai_debug_fini();

void samurai_debug(const char* func, const char* file, int line, const char *format, ...);
void samurai_error(const char* func, const char* file, int line, const char *format, ...);
void samurai_net(const char* func, const char* file, int line, const char *format, ...);
void samurai_search(const char* func, const char* file, int line, const char *format, ...);
void samurai_hub(const char* func, const char* file, int line, const char *format, ...);


#else /* ! DEBUG */

#if !defined(QDBG)
#define QDBG(format, ...) do { } while(0)
#endif

#if !defined(QERR)
#define QERR(format, ...) do { fprintf(stderr, "ERROR: "); fprintf(stderr, format, ## __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

#if !defined(QNET)
#define QNET(format, ...) do { } while(0)
#endif

#if !defined(QSEARCH)
#define QSEARCH(format, ...)  do { } while(0)
#endif

#if !defined(QHUB)
#define QHUB(format, ...)  do { } while(0)
#endif

#if !defined(QDBG_INIT)
#define QDBG_INIT do { } while(0)
#endif

#if !defined(QDBG_FINI)
#define QDBG_FINI do { } while(0)
#endif


#endif // DEBUG

#endif // HAVE_SAMURAI_DEBUG_DBG_H
