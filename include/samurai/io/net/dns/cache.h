/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SYSTEM_DNS_CACHE_H
#define HAVE_SYSTEM_DNS_CACHE_H

#include <samurai/samurai.h>
#include <samurai/io/net/dns/common.h>
#include <samurai/io/net/dns/dnsutil.h>
#include <samurai/io/net/dns/dnsrrs.h>
#include <vector>

namespace Samurai {
namespace IO {
namespace Net {
namespace DNS {

/**
 * A time-to-live bounded store of resource records.
 *
 * The cache owns every record it holds, so a record handed to add() must not be
 * owned by anything else - Message::releaseRecords() is how a decoded message
 * gives its records up.
 */
class CacheStorage {
	public:
		static CacheStorage* getInstance();

		~CacheStorage();

		/**
		 * Takes ownership of 'e' and starts its lifetime. Passing null does
		 * nothing. The oldest entry is dropped once the cache is full.
		 */
		void add(ResourceRecord* e);

		/** Drop and destroy every record whose time to live has run out. */
		void expire();

		/**
		 * @return the first unexpired record held for 'name', or null. The
		 *         record remains owned by the cache and only stays valid until
		 *         the next call that can expire it.
		 */
		ResourceRecord* lookup(const Name& name);

		size_t size() const { return cache.size(); }

		/* Owns raw pointers, so a copy would release them twice. */
		CacheStorage(const CacheStorage&) = delete;
		CacheStorage& operator=(const CacheStorage&) = delete;

	protected:
		CacheStorage();

		static CacheStorage* g_dns_cache;

	private:
		std::vector<ResourceRecord*> cache;
};


}
}
}
}

#endif // HAVE_SYSTEM_DNS_CACHE_H
