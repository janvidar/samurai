/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/dns/common.h>
#include <samurai/io/net/dns/dnsutil.h>
#include <samurai/io/net/dns/dnsrrs.h>

namespace Samurai {
namespace IO {
namespace Net {
namespace DNS {

class CacheStorage {
	public:
		static CacheStorage* getInstance();
		
		virtual ~CacheStorage();
		void add(ResourceRecord* e);
		void expire();
		ResourceRecord* lookup(const Name& name);

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
