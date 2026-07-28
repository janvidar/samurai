/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>

#include <sys/time.h>

#ifdef SAMURAI_POSIX
#include <sys/resource.h>
#include <sys/utsname.h>
#endif

#ifdef SAMURAI_OS_WINDOWS
#include <windows.h>
#include <winbase.h>
#include <process.h>
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <samurai/os.h>

namespace Samurai
{

class OSBase
{
	public:
		virtual ~OSBase() { }
		virtual size_t getMaxOpenSockets()  = 0;
		virtual time_t getUptime()          = 0;
		virtual pid_t getProcessID()        = 0;
		virtual std::string getHostName()   = 0;
		virtual std::string getDomainName() = 0;
		virtual const char* getName()       = 0;
		virtual const char* getVersion()    = 0;
};

#ifdef SAMURAI_POSIX
class OSUnix : public OSBase
{
	public:
		OSUnix();

	public:
		size_t getMaxOpenSockets();
		time_t getUptime();
		pid_t getProcessID();
		std::string getHostName();
		std::string getDomainName();
		const char* getName();
		const char* getVersion();
		
	private:
		struct rlimit  limits;
		struct utsname info;
};
#endif

#ifdef SAMURAI_OS_WINDOWS
class OSWindows : public OSBase
{
	public:
		OSWindows();

	public:
		size_t getMaxOpenSockets();
		time_t getUptime();
		pid_t getProcessID();
		std::string getHostName();
		std::string getDomainName();
		const char* getName();
		const char* getVersion();
};
#endif

}


#ifdef SAMURAI_POSIX
#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif

#define SAMURAI_FD_LIMIT_FALLBACK 1024
#define SAMURAI_FD_LIMIT_MIN        64
#define SAMURAI_FD_LIMIT_MAX     65536

Samurai::OSUnix::OSUnix()
{
	memset(&limits, 0, sizeof(limits));
	if (getrlimit(RLIMIT_NOFILE, &limits) != 0)
	{
		limits.rlim_cur = SAMURAI_FD_LIMIT_FALLBACK;
		limits.rlim_max = SAMURAI_FD_LIMIT_FALLBACK;
	}

	memset(&info, 0, sizeof(info));
	uname(&info);
}

size_t Samurai::OSUnix::getMaxOpenSockets()
{
	rlim_t soft = limits.rlim_cur;

	if (soft == RLIM_INFINITY || soft == 0)
		soft = SAMURAI_FD_LIMIT_FALLBACK;

	if (soft < SAMURAI_FD_LIMIT_MIN) soft = SAMURAI_FD_LIMIT_MIN;
	if (soft > SAMURAI_FD_LIMIT_MAX) soft = SAMURAI_FD_LIMIT_MAX;

	return (size_t) soft;
}

time_t Samurai::OSUnix::getUptime()
{
	return 0;
}

std::string Samurai::OSUnix::getHostName()
{
	/* NOTE: was a static buffer, so the returned pointer aliased across calls. */
	char hostname[MAXHOSTNAMELEN] = {0, };
	if (gethostname(hostname, MAXHOSTNAMELEN-1) == 0) {
		hostname[MAXHOSTNAMELEN-1] = 0;
		return std::string(hostname);
	}
	return std::string("localhost");
}

std::string Samurai::OSUnix::getDomainName()
{
	char hostname[MAXHOSTNAMELEN] = {0, };
	if (getdomainname(hostname, MAXHOSTNAMELEN) == 0) {
		hostname[MAXHOSTNAMELEN-1] = 0;
		return std::string(hostname);
	}
	return std::string("localdomain");
}

pid_t Samurai::OSUnix::getProcessID()
{
	return getpid();
}

const char* Samurai::OSUnix::getName()
{
	return info.sysname;
}

const char* Samurai::OSUnix::getVersion()
{
	return info.release;
}


#endif


#ifdef SAMURAI_OS_WINDOWS

Samurai::OSWindows::OSWindows()
{
}

size_t Samurai::OSWindows::getMaxOpenSockets()
{
	return 256; // FIXME
}

time_t Samurai::OSWindows::getUptime()
{
	return 0; // FIXME
}

std::string Samurai::OSWindows::getHostName()
{
/*
	static char hostname[MAXHOSTNAMELEN] = { 0, };
	int nSize = MAXHOSTNAMELEN - 1;
	if (GetComputerNameEx(ComputerNameNetBIOS, computer, &nSize) != FALSE) {
		return hostname;	
	}
*/
	return "localhost";
}

std::string Samurai::OSWindows::getDomainName()
{
	return "localdomain";
}

pid_t Samurai::OSWindows::getProcessID()
{
	return _getpid();
}

const char* Samurai::OSWindows::getName()
{
	return "Windows"; // FIXME
}

const char* Samurai::OSWindows::getVersion()
{
	return "Windows"; // FIXME
}

#endif


Samurai::OSBase* Samurai::OS::getInstance()
{
#ifdef SAMURAI_POSIX
	static OSUnix os;
#endif

#ifdef SAMURAI_OS_WINDOWS
	static OSWindows os;
#endif

	return &os;
}


size_t Samurai::OS::getMaxOpenSockets()
{
	return getInstance()->getMaxOpenSockets();
}

time_t Samurai::OS::getUptime()
{
	return getInstance()->getUptime();
}

std::string Samurai::OS::getHostName()
{
	return getInstance()->getHostName();
}

std::string Samurai::OS::getDomainName()
{
	return getInstance()->getDomainName();
}

pid_t Samurai::OS::getProcessID()
{
	return getInstance()->getProcessID();
}

const char* Samurai::OS::getName()
{
	return getInstance()->getName();
}

const char* Samurai::OS::getVersion()
{
	return getInstance()->getVersion();
}
