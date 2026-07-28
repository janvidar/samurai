/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <stdio.h>
#include <samurai/io/net/dns/resolver.h>
#include <memory>
#include <samurai/io/net/dns/resolver-blocking.h>
#include <samurai/io/net/dns/resolver-builtin.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/inetaddress.h>


Samurai::IO::Net::DNS::Resolver::Resolver(Samurai::IO::Net::ResolveEventHandler* eh) : eventHandler(eh)
{
	
}

Samurai::IO::Net::DNS::Resolver::~Resolver()
{
	eventHandler = nullptr;
}

/* static */
std::unique_ptr<Samurai::IO::Net::DNS::Resolver> Samurai::IO::Net::DNS::Resolver::getHostByName(Samurai::IO::Net::ResolveEventHandler* eh, const char* name)
{
#ifdef DNS_RESOLVE_BUILTIN
	auto resolver = std::make_unique<Samurai::IO::Net::DNS::BuiltinResolver>(eh);
#else
	auto resolver = std::make_unique<Samurai::IO::Net::DNS::BlockingResolver>(eh);
#endif

	resolver->lookup(name);
	return resolver;
}

/* static */
std::unique_ptr<Samurai::IO::Net::DNS::Resolver> Samurai::IO::Net::DNS::Resolver::getNameByAddress(Samurai::IO::Net::ResolveEventHandler*, InetAddress* address)
{
	(void) address;

	return nullptr;
}


