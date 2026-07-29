/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <stdio.h>
#include <samurai/io/net/dns/resolver.h>
#include <memory>
#include <samurai/io/net/dns/resolver-pool.h>
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
	auto resolver = std::make_unique<Samurai::IO::Net::DNS::PooledResolver>(eh);
	resolver->lookup(name);
	return resolver;
}

/* static */
std::unique_ptr<Samurai::IO::Net::DNS::Resolver> Samurai::IO::Net::DNS::Resolver::getNameByAddress(Samurai::IO::Net::ResolveEventHandler* eh, InetAddress* address)
{
	if (!address) return nullptr;

	auto resolver = std::make_unique<Samurai::IO::Net::DNS::PooledResolver>(eh);
	resolver->lookupAddress(*address);
	return resolver;
}


