/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <samurai/debug/dbg.h>
#include <samurai/io/file.h>
#include <memory>
#include <mutex>
#include <string>


#define PREFIX "../"

#ifdef SAMURAI_OS_WINDOWS
#define ENDLINE "\r\n"
#else
#define ENDLINE "\n"
#endif

#define DUPLICATE_TO_DEBUG


static std::unique_ptr<Samurai::IO::File> samurai_dump_net;
static std::unique_ptr<Samurai::IO::File> samurai_dump_sch;
static std::unique_ptr<Samurai::IO::File> samurai_dump_dbg;
static std::unique_ptr<Samurai::IO::File> samurai_dump_hub;

static bool g_debug_stderr = false;

/*
 * The logs are the process's, written by every loop's thread: opened by the first
 * that asks and closed by the last, with every write under the lock.
 */
static std::mutex samurai_dump_lock;
static int samurai_dump_users = 0;

void samurai_debug_init(const char* directory) {
	std::lock_guard<std::mutex> guard(samurai_dump_lock);
	if (samurai_dump_users++ > 0) return;

	fprintf(stderr, "Starting up with debug code enabled\n");

	const bool own = directory && *directory;
	const std::string dir = own ? directory : "~/.samurai/debug";
	if (!own) Samurai::IO::File::mkdir("~/.samurai", 0700);
	Samurai::IO::File::mkdir(dir.c_str(), 0700);

	if (getenv("SAMURAI_DEBUG"))
	{
		g_debug_stderr = true;
		fprintf(stderr, "    (dumping debug messages to stderr)\n");
	}

	samurai_dump_net = std::make_unique<Samurai::IO::File>(dir + "/network.log");
	samurai_dump_sch = std::make_unique<Samurai::IO::File>(dir + "/search.log");
	samurai_dump_dbg = std::make_unique<Samurai::IO::File>(dir + "/debug.log");
	samurai_dump_hub = std::make_unique<Samurai::IO::File>(dir + "/hub.log");
	samurai_dump_net->open(Samurai::IO::File::Mode::Write | Samurai::IO::File::Mode::Truncate);
	samurai_dump_sch->open(Samurai::IO::File::Mode::Write | Samurai::IO::File::Mode::Append);
	samurai_dump_dbg->open(Samurai::IO::File::Mode::Write | Samurai::IO::File::Mode::Truncate);
	samurai_dump_hub->open(Samurai::IO::File::Mode::Write | Samurai::IO::File::Mode::Append);
}

void samurai_debug_fini() {
	std::lock_guard<std::mutex> guard(samurai_dump_lock);
	if (samurai_dump_users == 0 || --samurai_dump_users > 0) return;

	samurai_dump_sch.reset();
	samurai_dump_hub.reset();
	samurai_dump_dbg.reset();
	samurai_dump_net.reset();
}

void samurai_debug(const char* /*func*/, const char* file, int line, const char *format, ...) {
	std::lock_guard<std::mutex> guard(samurai_dump_lock);
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
	std::lock_guard<std::mutex> guard(samurai_dump_lock);
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
	std::lock_guard<std::mutex> guard(samurai_dump_lock);
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
	std::lock_guard<std::mutex> guard(samurai_dump_lock);
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
	std::lock_guard<std::mutex> guard(samurai_dump_lock);
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



