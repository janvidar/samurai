/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <stdio.h>
#include <stdlib.h>

#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/net/dns/resolver.h>

static bool running = true;

using namespace Samurai::IO::Net;

class DNSResolver : public ResolveEventHandler {
	public:
	
		void lookup(const char* host)
		{
			Samurai::IO::Net::DNS::Resolver::getHostByName(this, host);
		}
	
		void EventHostFound(InetAddress* addr) {
			const char* str = addr->toString();
			if (str) {
				printf("%s\n", str);
			} else {
				printf("error: str is null\n");
			}
			running = false;
		}
		
		void EventHostError(enum Samurai::IO::Net::DNS::Resolver::Error error) {
			(void) error;
			puts("Lookup failed\n");
			running = false;
		}
};


int main(int argc, char** argv) {
	char* host = 0;
	if (argc > 1) host = argv[1];
	if (!host) {
		printf("Usage: %s address\n", argv[0]);
		printf("A simple dns lookup utility parser\n");
		exit(-1);
	}
	
	SocketMonitor* monitor = SocketMonitor::getInstance();
	DNSResolver resolver;
	
	resolver.lookup(host);
	while (running) {
		monitor->wait(25);
	}
}

