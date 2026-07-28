/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <samurai/debug/dbg.h>
#include <samurai/io/file.h>

#define DUMP_FILE_NETWORK "~/.samurai/debug/network.log"
#define DUMP_FILE_SEARCH  "~/.samurai/debug/search.log"
#define DUMP_FILE_DEBUG   "~/.samurai/debug/debug.log"
#define DUMP_FILE_HUB     "~/.samurai/debug/hub.log"
#define DUMP_FILE_MEM     "~/.samurai/debug/memory.log"

#define PREFIX "../"

#ifdef SAMURAI_OS_WINDOWS
#define ENDLINE "\r\n"
#else
#define ENDLINE "\n"
#endif

#define DUPLICATE_TO_DEBUG


static Samurai::IO::File* samurai_dump_net;
static Samurai::IO::File* samurai_dump_sch;
static Samurai::IO::File* samurai_dump_dbg;
static Samurai::IO::File* samurai_dump_hub;
#ifdef SAMURAI_MEMDBG
static Samurai::IO::File* samurai_dump_mem;
#endif

static bool g_debug_stderr = false;

void samurai_debug_init() {
	fprintf(stderr, "Starting up with debug code enabled\n");
	
	Samurai::IO::File::mkdir("~/.samurai",       0700);
	Samurai::IO::File::mkdir("~/.samurai/debug",   0700);
	
	if (getenv("SAMURAI_DEBUG"))
	{
		g_debug_stderr = true;
		fprintf(stderr, "    (dumping debug messages to stderr)\n");
	}
	
#ifdef SAMURAI_MEMDBG
	samurai_dump_mem = new Samurai::IO::File(DUMP_FILE_MEM);
	samurai_dump_mem->open(Samurai::IO::File::Write | Samurai::IO::File::Truncate);
#endif
	
	samurai_dump_net = new Samurai::IO::File(DUMP_FILE_NETWORK);
	samurai_dump_sch = new Samurai::IO::File(DUMP_FILE_SEARCH);
	samurai_dump_dbg = new Samurai::IO::File(DUMP_FILE_DEBUG);
	samurai_dump_hub = new Samurai::IO::File(DUMP_FILE_HUB);
	samurai_dump_net->open(Samurai::IO::File::Write | Samurai::IO::File::Truncate);
	samurai_dump_sch->open(Samurai::IO::File::Write | Samurai::IO::File::Append);
	samurai_dump_dbg->open(Samurai::IO::File::Write | Samurai::IO::File::Truncate);
	samurai_dump_hub->open(Samurai::IO::File::Write | Samurai::IO::File::Append);
	
}

void samurai_debug_fini() {
	delete samurai_dump_sch; samurai_dump_sch = 0;
	delete samurai_dump_hub; samurai_dump_hub = 0;
	delete samurai_dump_dbg; samurai_dump_dbg = 0;
	delete samurai_dump_net; samurai_dump_net = 0;
#ifdef SAMURAI_MEMDBG
	delete samurai_dump_mem; samurai_dump_mem = 0;
#endif

}

void samurai_debug(const char* /*func*/, const char* file, int line, const char *format, ...) {
	char logmsg[1024];
	char location[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(logmsg, 1024, format, args);
	va_end(args);
	
	// Strip the boring path details from the filename
	const char *separator = PREFIX;
	const char *shortfile = strstr(file, separator);
	shortfile = (shortfile ? shortfile + strlen(separator) + 1 : file);
	snprintf(location, 1024, "%32s:%u\t ", shortfile, line);
	
	if (!samurai_dump_dbg || g_debug_stderr)
	{
		fprintf(stderr, "DEBUG: %s: %s\n", location, logmsg);
		return;
	}
	samurai_dump_dbg->write(logmsg, strlen(logmsg));
	samurai_dump_dbg->write(ENDLINE, strlen(ENDLINE));
#ifdef FLUSH_DEBUG
	samurai_dump_dbg->flush();
#endif
}

void samurai_error(const char* /*func*/, const char* file, int line, const char *format, ...) {
	char logmsg[1024];
	char location[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(logmsg, 1024, format, args);
	va_end(args);
	
	// Strip the boring path details from the filename
	const char *separator = PREFIX;
	const char *shortfile = strstr(file, separator);
	shortfile = (shortfile ? shortfile + strlen(separator) + 1 : file);
	snprintf(location, 1024, "%32s:%u\t ", shortfile, line);
	fprintf(stderr, "ERROR: %s: %s\n", location, logmsg);
	
	if (!samurai_dump_dbg) return;
	samurai_dump_dbg->write("ERROR: ", strlen("ERROR: "));
	samurai_dump_dbg->write(location, strlen(location));
	samurai_dump_dbg->write(": ", strlen(": "));
	samurai_dump_dbg->write(logmsg, strlen(logmsg));
	samurai_dump_dbg->write(ENDLINE, strlen(ENDLINE));
#ifdef FLUSH_DEBUG
	samurai_dump_dbg->flush();
#endif
}

void samurai_net(const char*, const char* , int, const char *format, ...) {
	if (!samurai_dump_net) return;
	
	char logmsg[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(logmsg, 1024, format, args);
	va_end(args);
	
	samurai_dump_net->write(logmsg, strlen(logmsg));
	samurai_dump_net->write(ENDLINE, strlen(ENDLINE));
#ifdef FLUSH_DEBUG
	samurai_dump_net->flush();
#endif

#ifdef DUPLICATE_TO_DEBUG
        if (!samurai_dump_dbg) return;
        samurai_dump_dbg->write(logmsg, strlen(logmsg));
        samurai_dump_dbg->write(ENDLINE, strlen(ENDLINE));
#ifdef FLUSH_DEBUG
        samurai_dump_dbg->flush();
#endif
#endif

}

void samurai_search(const char*, const char* , int, const char *format, ...) {
	if (!samurai_dump_sch) return;
	
	char logmsg[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(logmsg, 1024, format, args);
	va_end(args);
	
	samurai_dump_sch->write(logmsg, strlen(logmsg));
	samurai_dump_sch->write(ENDLINE, strlen(ENDLINE));
#ifdef FLUSH_DEBUG
	samurai_dump_sch->flush();
#endif

#ifdef DUPLICATE_TO_DEBUG
        if (!samurai_dump_dbg) return;
        samurai_dump_dbg->write(logmsg, strlen(logmsg));
        samurai_dump_dbg->write(ENDLINE, strlen(ENDLINE));
#ifdef FLUSH_DEBUG
        samurai_dump_dbg->flush();
#endif
#endif


}

void samurai_hub(const char*, const char* , int, const char *format, ...) {
	if (!samurai_dump_hub) return;
	
	char logmsg[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(logmsg, 1024, format, args);
	va_end(args);
	
	samurai_dump_hub->write(logmsg, strlen(logmsg));
	samurai_dump_hub->write(ENDLINE, strlen(ENDLINE));
#ifdef FLUSH_DEBUG
	samurai_dump_hub->flush();
#endif
#ifdef DUPLICATE_TO_DEBUG
        if (!samurai_dump_dbg) return;
        samurai_dump_dbg->write(logmsg, strlen(logmsg));
        samurai_dump_dbg->write(ENDLINE, strlen(ENDLINE));
#ifdef FLUSH_DEBUG
        samurai_dump_dbg->flush();
#endif
#endif


}

#ifdef SAMURAI_MEMDBG
void samurai_memory(const char* func, void* addr, size_t size, void* code_addr, void* code_addr_up) {
	if (!samurai_dump_mem) return;
	
	char logmsg[1024] = {0, };
	sprintf(logmsg, "%s: addr=%p, size=%u, stack=%p, stack=%p", func, addr, size, code_addr, code_addr_up);
	samurai_dump_mem->write(logmsg, strlen(logmsg));
	samurai_dump_mem->write(ENDLINE, strlen(ENDLINE));
#ifdef FLUSH_DEBUG
	samurai_dump_mem->flush();
#endif

#ifdef DUPLICATE_TO_DEBUG
        if (!samurai_dump_dbg) return;
        samurai_dump_dbg->write(logmsg, strlen(logmsg));
        samurai_dump_dbg->write(ENDLINE, strlen(ENDLINE));
#ifdef FLUSH_DEBUG
        samurai_dump_dbg->flush();
#endif
#endif

}
#endif



