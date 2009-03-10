/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <stdio.h>
#include <samurai/io/net/dns/resolver.h>
#include <samurai/io/net/dns/resolver-blocking.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/inetaddress.h>

/*
 * This is mainly usefull for debugging. To prevent GDB from exitting
 * when the forked process is exiting.
 */

Samurai::IO::Net::DNS::BlockingResolver::BlockingResolver(ResolveEventHandler* eh) : Samurai::IO::Net::DNS::Resolver(eh)
{

}

Samurai::IO::Net::DNS::BlockingResolver::~BlockingResolver() {

}

void Samurai::IO::Net::DNS::BlockingResolver::lookup(const char* address) {
	struct hostent* host = gethostbyname(address);
	
	if (!host) {
		switch (h_errno) {
			case HOST_NOT_FOUND:
				eventHandler->EventHostError(NotFound);
				break;

			// case NO_DATA:
			case NO_ADDRESS:
				eventHandler->EventHostError(NoAddress);
				break;

			case NO_RECOVERY:
				eventHandler->EventHostError(ServerError);
				break;
			
			case TRY_AGAIN:
				eventHandler->EventHostError(TryAgain);
				break;
				
			default:
				eventHandler->EventHostError(Unknown);
		}
		return;
	}
	
	Samurai::IO::Net::InetAddress inet_addr;
	inet_addr.setRawAddress(host->h_addr_list[0], host->h_length, host->h_addrtype == AF_INET ? Samurai::IO::Net::InetAddress::IPv4 : Samurai::IO::Net::InetAddress::IPv6);
	if (host)
		eventHandler->EventHostFound(&inet_addr);
	else
		eventHandler->EventHostError(Unknown);
}

