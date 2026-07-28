/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_DNSRESOLVER_BLOCKING_H
#define HAVE_SAMURAI_DNSRESOLVER_BLOCKING_H

#include <samurai/io/net/dns/resolver.h>

namespace Samurai {
namespace IO {
namespace Net {
namespace DNS {

class BlockingResolver : public Resolver {
	public:
		virtual ~BlockingResolver();
		BlockingResolver(ResolveEventHandler* eventHandler);
		void lookup(const char* addr);

	protected:
		char* ipaddr;
};

}
}
}
}

#endif // HAVE_SAMURAI_DNSRESOLVER_BLOCKING_H
