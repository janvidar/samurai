/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_OS_ABSTRACTIONS_H
#define HAVE_OS_ABSTRACTIONS_H

#include <samurai/samurai.h>
#include <string>

namespace Samurai {

class OSBase;

class OS {
	public:
		static size_t getMaxOpenSockets();
		static time_t getUptime();
		static std::string getHostName();
		static std::string getDomainName();
		static pid_t getProcessID();
		
		/**
		 * Returns the operating system name, such as Linux, FreeBSD, Darwin or Windows.
		 */
		static const char* getName();
		
		/**
		 * Returns the operating system vesion.
		 *
		 * Example: "2.6.29" for Linux kernel 2.6.29
		 */
		static const char* getVersion();
		
	private:
		static OSBase* getInstance();
};


}

#endif // HAVE_OS_ABSTRACTIONS_H

