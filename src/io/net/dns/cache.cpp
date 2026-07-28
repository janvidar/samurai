/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/timestamp.h>
#include <samurai/io/net/dns/cache.h>

Samurai::IO::Net::DNS::CacheStorage* Samurai::IO::Net::DNS::CacheStorage::g_dns_cache = nullptr;

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
	for (ResourceRecord* record : cache)
		delete record;
	cache.clear();
}

void Samurai::IO::Net::DNS::CacheStorage::add(ResourceRecord* e)
{
	if (!e) return;

	e->stampExpiry();

	/* Make room by dropping what has run out first, and only evict a live
	   record if that was not enough. */
	expire();
	while (cache.size() >= DNS_CACHE_MAX_STORAGE) {
		delete cache.front();
		cache.erase(cache.begin());
	}

	QDBG("Adding RR to cache. Expire: %d", e->getTimeToLive());
	cache.push_back(e);
}

void Samurai::IO::Net::DNS::CacheStorage::expire()
{
	/*
	 * erase() invalidates the iterator it is given, so the survivors are
	 * gathered into a new vector rather than erased in place - which is what
	 * makes advancing past an erased element impossible to get wrong.
	 */
	std::vector<ResourceRecord*> live;
	live.reserve(cache.size());

	for (ResourceRecord* record : cache) {
		if (record->isExpired()) {
			QDBG("Expiring cached RR");
			delete record;
		} else {
			live.push_back(record);
		}
	}

	cache.swap(live);
}

Samurai::IO::Net::DNS::ResourceRecord* Samurai::IO::Net::DNS::CacheStorage::lookup(const Samurai::IO::Net::DNS::Name& name)
{
	expire();

	for (ResourceRecord* record : cache) {
		if (record->name == name)
			return record;
	}

	return nullptr;
}
