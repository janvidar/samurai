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
		virtual const char* getHostName()   = 0;
		virtual const char* getDomainName() = 0;
		virtual const char* getOSName()     = 0;
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
		const char* getHostName();
		const char* getDomainName();
		const char* getOSName();
		
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
		const char* getHostName();
		const char* getDomainName();
		const char* getOSName();
};
#endif

}


#ifdef SAMURAI_POSIX
#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif

Samurai::OSUnix::OSUnix()
{
	getrlimit(RLIMIT_NOFILE, &limits);
	uname(&info);
}

size_t Samurai::OSUnix::getMaxOpenSockets()
{
	return (size_t) limits.rlim_max;
}

time_t Samurai::OSUnix::getUptime()
{
	return 0;
}

const char* Samurai::OSUnix::getHostName()
{
	static char hostname[MAXHOSTNAMELEN] = {0, };
	if (gethostname(hostname, MAXHOSTNAMELEN-1) == 0) {
		hostname[MAXHOSTNAMELEN-1] = 0;
		return hostname;
	}
	return "localhost";
}

const char* Samurai::OSUnix::getDomainName()
{
	static char hostname[MAXHOSTNAMELEN];
	if (getdomainname(hostname, MAXHOSTNAMELEN) == 0) {
		hostname[MAXHOSTNAMELEN-1] = 0;
		return hostname;
	}
	return "localdomain";
}

pid_t Samurai::OSUnix::getProcessID()
{
	return getpid();
}

const char* Samurai::OSUnix::getOSName()
{
	return info.sysname;
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

const char* Samurai::OSWindows::getHostName()
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

const char* Samurai::OSWindows::getDomainName()
{
	return "localdomain";
}

pid_t Samurai::OSWindows::getProcessID()
{
	return _getpid();
}

const char* Samurai::OSWindows::getOSName()
{
	return "Windows"; // FIXME
}
#endif


Samurai::OSBase* Samurai::OS::instance = 0;

Samurai::OSBase* Samurai::OS::getInstance()
{
	if (instance)
		return instance;
		
#ifdef SAMURAI_POSIX
	instance = new OSUnix();
#endif

#ifdef SAMURAI_OS_WINDOWS
	instance = new OSWindows();
#endif

	return instance;
}


size_t Samurai::OS::getMaxOpenSockets()
{
	return getInstance()->getMaxOpenSockets();
}

time_t Samurai::OS::getUptime()
{
	return getInstance()->getUptime();
}

const char* Samurai::OS::getHostName()
{
	return getInstance()->getHostName();
}

const char* Samurai::OS::getDomainName()
{
	return getInstance()->getDomainName();
}

pid_t Samurai::OS::getProcessID()
{
	return getInstance()->getProcessID();
}

const char* Samurai::OS::getOSName()
{
	return getInstance()->getOSName();
}

