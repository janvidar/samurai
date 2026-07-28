/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_DNS_RESOLVER_CONFIG_H
#define HAVE_DNS_RESOLVER_CONFIG_H

#include <samurai/samurai.h>
#include <samurai/io/net/dns/common.h>

namespace Samurai {
namespace IO {
namespace Net {
class InetAddress;
namespace DNS {

class ResolveConfiguration {
	public:
		ResolveConfiguration(const char* resolv_conf = "/etc/resolv.conf");
		virtual ~ResolveConfiguration();

		/**
		 * The configured name server to use for the given attempt, or 0 if
		 * none could be read from the configuration. Callers must check:
		 * a machine with no resolv.conf, or one listing only addresses that
		 * fail to parse, has nowhere to send a query.
		 */
		Samurai::IO::Net::InetAddress* getNameServer(size_t num_try = 0);
		size_t getNameServerCount() const { return num_nameservers; }
		void skipNameServer();
		char* getNameSearch();

		size_t getTimeout() const;
		size_t getAttempts() const;
		size_t getNDots() const;
		bool   isRotate() const;
		bool   isIPv6() const;
		bool   isDebug() const;

	protected:
		void parse(const char* resolv_conf);
		void parseLine(char* line);
		void addNameServer(const char* server);

	protected:
		size_t num_nameservers;
		size_t cur_nameserver;
		Samurai::IO::Net::InetAddress* nameservers[MAXNS];

		bool option_rotate;
		bool option_ipv6;
		bool option_debug;
		size_t option_timeout;
		size_t option_attempts;
		size_t option_ndots;
};

}
}
}
}

#endif // HAVE_DNS_RESOLVER_CONFIG_H

