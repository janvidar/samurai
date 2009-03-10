/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_QUICKDC_DNSRESOLVER_FORKED_H
#define HAVE_QUICKDC_DNSRESOLVER_FORKED_H

#include <samurai/io/net/dns/resolver.h>

#ifdef SAMURAI_POSIX

namespace Samurai {
namespace IO {
namespace Net {
namespace DNS {

class ForkResolver : public Resolver {
	public:
		virtual ~ForkResolver();
		ForkResolver(ResolveEventHandler* eventHandler);
		void lookup(const char* addr);

	protected:
		int childPid;
		int sd;
		void internal_lookup();
		friend class SocketMonitor;
		char* ipaddr;
};

}
}
}
}

#endif // SAMURAI_POSIX

#endif // HAVE_QUICKDC_DNSRESOLVER_FORKED_H
