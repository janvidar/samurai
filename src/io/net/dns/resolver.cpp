/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <stdio.h>
#include <samurai/io/net/dns/resolver.h>
#include <memory>
#include <samurai/io/net/dns/resolver-pool.h>
#include <samurai/io/net/dns/resolver-socks.h>
#include <samurai/io/net/proxy.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/inetaddress.h>


Samurai::IO::Net::DNS::Resolver::Resolver(Samurai::IO::Net::ResolveEventHandler* eh) : eventHandler(eh)
{
	
}

Samurai::IO::Net::DNS::Resolver::~Resolver()
{
	eventHandler = nullptr;
}

/*
 * NOTE: which backend answers depends on the process-wide proxy.
 *
 * With ProxySettings::setTorExtensions() on, the question goes to the proxy over
 * SOCKS5 rather than to the system resolver, which is the only way a program that
 * has to resolve a name for its own sake can do so without telling whatever
 * resolv.conf names who it is about to contact. SocksResolver returns null when
 * it cannot serve a particular lookup, and then the pool answers as it always
 * has - so the fallback is a decision made here, in one place, rather than a
 * failure the caller has to notice.
 */

/* static */
std::unique_ptr<Samurai::IO::Net::DNS::Resolver> Samurai::IO::Net::DNS::Resolver::getHostByName(Samurai::IO::Net::ResolveEventHandler* eh, const char* name)
{
	std::unique_ptr<Samurai::IO::Net::DNS::Resolver> via_proxy =
		Samurai::IO::Net::DNS::SocksResolver::forward(eh, name);
	if (via_proxy) return via_proxy;

	auto resolver = std::make_unique<Samurai::IO::Net::DNS::PooledResolver>(eh);
	resolver->lookup(name);
	return resolver;
}

/* static */
std::unique_ptr<Samurai::IO::Net::DNS::Resolver> Samurai::IO::Net::DNS::Resolver::getNameByAddress(Samurai::IO::Net::ResolveEventHandler* eh, InetAddress* address)
{
	if (!address) return nullptr;

	std::unique_ptr<Samurai::IO::Net::DNS::Resolver> via_proxy =
		Samurai::IO::Net::DNS::SocksResolver::reverse(eh, *address);
	if (via_proxy) return via_proxy;

	auto resolver = std::make_unique<Samurai::IO::Net::DNS::PooledResolver>(eh);
	resolver->lookupAddress(*address);
	return resolver;
}


