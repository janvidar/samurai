/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_DEFINES_H
#define HAVE_SAMURAI_DEFINES_H

/*
 * Operating system:
 * - SAMURAI_OS_LINUX
 * - SAMURAI_OS_SOLARIS
 * - SAMURAI_OS_MACOSX
 * - SAMURAI_OS_FREEBSD
 * - SAMURAI_OS_OPENBSD
 * - SAMURAI_OS_NETBSD
 * - SAMURAI_OS_WINDOWS_2000  (not tested)
 * - SAMURAI_OS_WINDOWS_XP    (not tested)
 * - SAMURAI_OS_WINDOWS_2003  (not tested)
 * - SAMURAI_OS_WINDOWS_VISTA (not tested)
 *
 * Operating system classes / capabilities
 * - SAMURAI_POSIX   (Posix compliant OSes)
 * - SAMURAI_UNIX    (Unix-like OSes)
 * - SAMURAI_BSD     (BSD-like unixes)
 * - SAMURAI_WINDOWS (Windows operating systems)
 *
 * CPU/Architecture
 * - SAMURAI_CPU_SPARC     - sparc32 cpu
 * - SAMURAI_CPU_SPARC64   - sparc64 cpu
 * - SAMURAI_CPU_POWERPC   - powerpc/ppc32
 * - SAMURAI_CPU_POWERPC64 - powerpc64
 * - SAMURAI_CPU_INTEL     - ia32/x86 processors
 * - SAMURAI_CPU_AMD64     - amd64/x86_64/intel emt
 *
 * CPU variants:
 * - SAMURAI_64BIT         - Only defined for 64bit CPUs
 * - SAMURAI_BIG_ENDIAN    - Only defined if big endian is being used.
 */


#if defined(__linux__) || defined(__linux) || defined(linux)
#define SAMURAI_OS_LINUX
#define SAMURAI_POSIX
#define SAMURAI_UNIX
#endif

#if defined(__sun__) || defined(__sun) || defined(sun)
#define SAMURAI_OS_SOLARIS
#define SAMURAI_POSIX
#define SAMURAI_UNIX
#endif

#ifdef __APPLE__
#define SAMURAI_OS_MACOSX
#define SAMURAI_POSIX
#define SAMURAI_BSD
#define SAMURAI_UNIX
#endif

#ifdef __FreeBSD__
#define SAMURAI_OS_FREEBSD
#define SAMURAI_POSIX
#define SAMURAI_BSD
#define SAMURAI_UNIX
#endif

#ifdef __NetBSD__
#define SAMURAI_OS_NETBSD
#define SAMURAI_POSIX
#define SAMURAI_BSD
#define SAMURAI_UNIX
#endif

#ifdef __OpenBSD__
#define SAMURAI_OS_OPENBSD
#define SAMURAI_POSIX
#define SAMURAI_BSD
#define SAMURAI_UNIX
#endif

#if defined(_WIN32) || defined(__MINGW32__) || defined(_WIN64)
#define SAMURAI_OS_WINDOWS
#define SAMURAI_WINDOWS
#endif

/* SPARC cpus */
#if defined(__sparc__) || defined(__sparc) || defined(sparc)
#ifdef __arch64__
#define SAMURAI_CPU_SPARC64
#define SAMURAI_64BIT
#else
#define SAMURAI_CPU_SPARC
#endif
#endif

/* PowerPC cpus */
#if defined(__POWERPC__) || defined(__ppc__) || defined(_ARCH_PPC)
#if defined(__ppc64__) || (_ARCH_PPC64)
#define SAMURAI_CPU_POWERPC64
#define SAMURAI_64BIT
#else
#define SAMURAI_CPU_POWERPC
#endif
#endif

/* intel cpus */
#if defined(__i386__) || defined(__i386) || defined(i386)
#define SAMURAI_CPU_INTEL
#endif

/* amd64 cpus */
#if defined(__x86_64__) || defined(__x86_64) || defined(__amd64__) || defined(__amd64)
#define SAMURAI_CPU_AMD64
#define SAMURAI_64BIT
#endif

/* Big endian */
#if defined(__BIG_ENDIAN__) || defined(_BIG_ENDIAN)
#define SAMURAI_BIG_ENDIAN
#endif



#endif // HAVE_SAMURAI_DEFINES_H
