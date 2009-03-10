/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef BUILD
#define BUILD "internal"
#endif

#if defined(DEBUG)

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if !defined(QDBG)
#define QDBG(format, ...)  do { QuickDC_Debug(__PRETTY_FUNCTION__, __FILE__, __LINE__, format, ## __VA_ARGS__); } while(0);
#endif

#if !defined(QERR)
#define QERR(format, ...)  do { QuickDC_Error(__PRETTY_FUNCTION__, __FILE__, __LINE__, format, ## __VA_ARGS__); } while(0);
#endif

#if !defined(QNET)
#define QNET(format, ...)  do { QuickDC_Net  (__PRETTY_FUNCTION__, __FILE__, __LINE__, format, ## __VA_ARGS__); } while(0);
#endif

#if !defined(QSEARCH)
#define QSEARCH(format, ...)  do { QuickDC_Search(__PRETTY_FUNCTION__, __FILE__, __LINE__, format, ## __VA_ARGS__); } while(0);
#endif

#if !defined(QHUB)
#define QHUB(format, ...)  do { QuickDC_Hub(__PRETTY_FUNCTION__, __FILE__, __LINE__, format, ## __VA_ARGS__); } while(0);
#endif

#if !defined(QDBG_INIT)
#define QDBG_INIT do { QuickDC_Debug_Init(); } while(0);
#endif

#if !defined(QDBG_FINI)
#define QDBG_FINI do { QuickDC_Debug_Fini(); } while(0);
#endif

void QuickDC_Debug_Init();
void QuickDC_Debug_Fini();

void QuickDC_Debug(const char* func, const char* file, int line, const char *format, ...);
void QuickDC_Error(const char* func, const char* file, int line, const char *format, ...);
void QuickDC_Net(const char* func, const char* file, int line, const char *format, ...);
void QuickDC_Search(const char* func, const char* file, int line, const char *format, ...);
void QuickDC_Hub(const char* func, const char* file, int line, const char *format, ...);

#ifdef QUICKDC_MEMDBG
void QuickDC_Memory(const char* func, void* addr, size_t size, void* code_addr, void* code_addr_up);
#endif


#else /* ! DEBUG */

#include <stdio.h>

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

