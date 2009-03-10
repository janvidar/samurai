/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/timestamp.h>
#include <samurai/io/net/dns/cache.h>

Samurai::IO::Net::DNS::CacheStorage* Samurai::IO::Net::DNS::CacheStorage::g_dns_cache = 0;

Samurai::IO::Net::DNS::CacheStorage* Samurai::IO::Net::DNS::CacheStorage::getInstance() 
{
	if (!g_dns_cache)
		g_dns_cache = new Samurai::IO::Net::DNS::CacheStorage();
	return g_dns_cache;
}


Samurai::IO::Net::DNS::CacheStorage::CacheStorage()
{
	QDBG("Creating DNS cache storage\n");
}

Samurai::IO::Net::DNS::CacheStorage::~CacheStorage()
{
	QDBG("Shutting down DNS cache storage\n");
}

void Samurai::IO::Net::DNS::CacheStorage::add(ResourceRecord* e)
{
	if (!e) return;
	
	QDBG("Adding RR to cache. Expire: %d", e->getTimeToLive());
	cache.push_back(e);
}

void Samurai::IO::Net::DNS::CacheStorage::expire()
{
	Samurai::TimeStamp now;
	for (std::vector<Samurai::IO::Net::DNS::ResourceRecord*>::iterator it = cache.begin(); it != cache.end(); it++)
	{
		/*
		Samurai::IO::Net::DNS::ResourceRecord* cached = (*it);
		if (now >= cached->expireTime) {
			// FIXME: Make sure we delete these.
			cache.erase(it);
			delete cached;
		}
		*/
	}
}

Samurai::IO::Net::DNS::ResourceRecord* Samurai::IO::Net::DNS::CacheStorage::lookup(const Samurai::IO::Net::DNS::Name& name)
{
	(void) name;

	Samurai::TimeStamp now;
	for (std::vector<ResourceRecord*>::iterator it = cache.begin(); it != cache.end(); it++)
	{
		/*
			Samurai::IO::Net::DNS::ResourceRecord* cached = (*it);
			if (!strcasecmp(name, (const char*) cached->hostname) && now < cached->expireTime) {
				return cached;
			}
		*/
	}
	return 0;
}

