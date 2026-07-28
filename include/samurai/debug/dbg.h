/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef BUILD
#define BUILD "internal"
#endif

/*
 * NOTE: These are included for both configurations on purpose. They used to be
 * inside the #ifdef DEBUG branch below, so the set of headers this file pulled
 * in changed with the build type - and the ~18 translation units that reach
 * memcpy()/memset()/strcmp() through this header compiled with DEBUG but not
 * without it. The old Makefile defaulted to DEBUG=YES, so a release build was
 * never attempted.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(DEBUG)

#if !defined(QDBG)
#define QDBG(format, ...)  do { samurai_debug(__PRETTY_FUNCTION__, __FILE__, __LINE__, format, ## __VA_ARGS__); } while(0);
#endif

#if !defined(QERR)
#define QERR(format, ...)  do { samurai_error(__PRETTY_FUNCTION__, __FILE__, __LINE__, format, ## __VA_ARGS__); } while(0);
#endif

#if !defined(QNET)
#define QNET(format, ...)  do { samurai_net(__PRETTY_FUNCTION__, __FILE__, __LINE__, format, ## __VA_ARGS__); } while(0);
#endif

#if !defined(QSEARCH)
#define QSEARCH(format, ...)  do { samurai_search(__PRETTY_FUNCTION__, __FILE__, __LINE__, format, ## __VA_ARGS__); } while(0);
#endif

#if !defined(QHUB)
#define QHUB(format, ...)  do { samurai_hub(__PRETTY_FUNCTION__, __FILE__, __LINE__, format, ## __VA_ARGS__); } while(0);
#endif

#if !defined(QDBG_INIT)
#define QDBG_INIT do { samurai_debug_init(); } while(0);
#endif

#if !defined(QDBG_FINI)
#define QDBG_FINI do { samurai_debug_fini(); } while(0);
#endif

void samurai_debug_init();
void samurai_debug_fini();

void samurai_debug(const char* func, const char* file, int line, const char *format, ...);
void samurai_error(const char* func, const char* file, int line, const char *format, ...);
void samurai_net(const char* func, const char* file, int line, const char *format, ...);
void samurai_search(const char* func, const char* file, int line, const char *format, ...);
void samurai_hub(const char* func, const char* file, int line, const char *format, ...);

#ifdef SAMURAI_MEMDBG
void samurai_memory(const char* func, void* addr, size_t size, void* code_addr, void* code_addr_up);
#endif


#else /* ! DEBUG */

#if !defined(QDBG)
#define QDBG(format, ...) do { } while(0);
#endif

#if !defined(QERR)
#define QERR(format, ...) do { fprintf(stderr, "ERROR: "); fprintf(stderr, format, ## __VA_ARGS__); fprintf(stderr, "\n"); } while(0);
#endif

#if !defined(QNET)
#define QNET(format, ...) do { } while(0);
#endif

#if !defined(QSEARCH)
#define QSEARCH(format, ...)  do { } while(0);
#endif

#if !defined(QHUB)
#define QHUB(format, ...)  do { } while(0);
#endif

#if !defined(QDBG_INIT)
#define QDBG_INIT do { } while(0);
#endif

#if !defined(QDBG_FINI)
#define QDBG_FINI do { } while(0);
#endif


#endif // DEBUG

