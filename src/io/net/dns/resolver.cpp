/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <stdio.h>
#include <samurai/io/net/dns/resolver.h>
#include <samurai/io/net/dns/resolver-blocking.h>
#include <samurai/io/net/dns/resolver-builtin.h>
#include <samurai/io/net/dns/resolver-fork.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/inetaddress.h>


Samurai::IO::Net::DNS::Resolver::Resolver(Samurai::IO::Net::ResolveEventHandler* eh) : eventHandler(eh)
{
	
}

Samurai::IO::Net::DNS::Resolver::~Resolver()
{
	eventHandler = 0;
}

/* static */
Samurai::IO::Net::DNS::Resolver* Samurai::IO::Net::DNS::Resolver::getHostByName(Samurai::IO::Net::ResolveEventHandler* eh, const char* name)
{
	Samurai::IO::Net::DNS::Resolver* resolver = 0;
#ifdef DNS_RESOLVE_BLOCKING
	resolver = new Samurai::IO::Net::DNS::BlockingResolver(eh);
#else
# ifdef DNS_RESOLVE_FORKED
	resolver = new Samurai::IO::Net::DNS::ForkResolver(eh);
# else
#  ifdef DNS_RESOLVE_BUILTIN
	resolver = new Samurai::IO::Net::DNS::BuiltinResolver(eh);
#  else
	resolver = new Samurai::IO::Net::DNS::BlockingResolver(eh);
#  endif // DNS_RESOLVE_BUILTIN
# endif //DNS_RESOLVE_FORK
#endif // DNS_RESOLVE_BLOCKING

/*
	printf("Looking up host \"%s\"...\n", name);
*/

	if (resolver) resolver->lookup(name);
	return resolver;
}

/* static */
Samurai::IO::Net::DNS::Resolver* Samurai::IO::Net::DNS::Resolver::getNameByAddress(Samurai::IO::Net::ResolveEventHandler*, InetAddress* address)
{
	(void) address;
	
	return 0;
}


