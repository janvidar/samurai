/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <stdio.h>
#include <string.h>
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

/*
 * NOTE: getaddrinfo(), not gethostbyname(): it is reentrant and returns both
 * address families.
 */
void Samurai::IO::Net::DNS::BlockingResolver::lookup(const char* address) {
	if (!eventHandler) return;

	if (!address || !*address) {
		eventHandler->EventHostError(Samurai::IO::Net::DNS::Resolver::Error::NotFound);
		return;
	}

	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family   = AF_UNSPEC;   /* IPv4 and IPv6 */
	hints.ai_socktype = SOCK_STREAM;

	struct addrinfo* result = nullptr;
	const int rc = ::getaddrinfo(address, 0, &hints, &result);

	if (rc != 0) {
		switch (rc) {
			case EAI_NONAME:  eventHandler->EventHostError(Samurai::IO::Net::DNS::Resolver::Error::NotFound);    break;
			case EAI_AGAIN:   eventHandler->EventHostError(Samurai::IO::Net::DNS::Resolver::Error::TryAgain);    break;
			case EAI_FAIL:    eventHandler->EventHostError(Samurai::IO::Net::DNS::Resolver::Error::ServerError); break;
#if defined(EAI_NODATA) && EAI_NODATA != EAI_NONAME
			case EAI_NODATA:  eventHandler->EventHostError(Samurai::IO::Net::DNS::Resolver::Error::NoAddress);   break;
#endif
			case EAI_MEMORY:  eventHandler->EventHostError(Samurai::IO::Net::DNS::Resolver::Error::ServerError); break;
			default:          eventHandler->EventHostError(Samurai::IO::Net::DNS::Resolver::Error::Unknown);
		}
		return;
	}

	Samurai::IO::Net::InetAddress inet_addr;
	bool found = false;

	/* getaddrinfo() returns candidates in the order the platform prefers
	   (RFC 6724 on a current system), so the first usable one is the one to
	   take. */
	for (struct addrinfo* ai = result; ai && !found; ai = ai->ai_next) {
		if (ai->ai_family == AF_INET && ai->ai_addrlen >= sizeof(struct sockaddr_in)) {
			struct sockaddr_in* sin = (struct sockaddr_in*) ai->ai_addr;
			found = inet_addr.setRawAddress(&sin->sin_addr, sizeof(sin->sin_addr),
			                                Samurai::IO::Net::InetAddress::IPv4);
		} else if (ai->ai_family == AF_INET6 && ai->ai_addrlen >= sizeof(struct sockaddr_in6)) {
			struct sockaddr_in6* sin6 = (struct sockaddr_in6*) ai->ai_addr;
			found = inet_addr.setRawAddress(&sin6->sin6_addr, sizeof(sin6->sin6_addr),
			                                Samurai::IO::Net::InetAddress::IPv6);
		}
	}

	::freeaddrinfo(result);

	if (found)
		eventHandler->EventHostFound(&inet_addr);
	else
		eventHandler->EventHostError(Samurai::IO::Net::DNS::Resolver::Error::NoAddress);
}
