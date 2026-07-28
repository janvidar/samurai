/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <memory>
#include <algorithm>
#include <samurai/samurai.h>
#include <samurai/timestamp.h>
#include <samurai/io/net/dns/cache.h>

/*
 * A function-local static is initialised once, and without the check-then-assign
 * race the raw pointer version had. It is also destroyed at exit, which releases
 * the cached records.
 */
Samurai::IO::Net::DNS::CacheStorage* Samurai::IO::Net::DNS::CacheStorage::getInstance()
{
	static Samurai::IO::Net::DNS::CacheStorage cache;
	return &cache;
}


Samurai::IO::Net::DNS::CacheStorage::CacheStorage()
{
	QDBG("Creating DNS cache storage\n");
}

Samurai::IO::Net::DNS::CacheStorage::~CacheStorage()
{
	QDBG("Shutting down DNS cache storage\n");
}

void Samurai::IO::Net::DNS::CacheStorage::add(std::unique_ptr<ResourceRecord> e)
{
	if (!e) return;

	e->stampExpiry();

	/* Make room by dropping what has run out first, and only evict a live
	   record if that was not enough. */
	expire();
	while (cache.size() >= DNS_CACHE_MAX_STORAGE)
		cache.erase(cache.begin());

	QDBG("Adding RR to cache. Expire: %d", e->getTimeToLive());
	cache.push_back(std::move(e));
}

void Samurai::IO::Net::DNS::CacheStorage::expire()
{
	std::erase_if(cache, [](const std::unique_ptr<ResourceRecord>& record) {
		if (!record->isExpired()) return false;
		QDBG("Expiring cached RR");
		return true;
	});
}

Samurai::IO::Net::DNS::ResourceRecord* Samurai::IO::Net::DNS::CacheStorage::lookup(const Samurai::IO::Net::DNS::Name& name)
{
	expire();

	for (const std::unique_ptr<ResourceRecord>& record : cache) {
		if (record->name == name)
			return record.get();
	}

	return nullptr;
}
