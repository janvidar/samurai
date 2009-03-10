/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SYSTEM_NET_INET_ADDRESS_H
#define HAVE_SYSTEM_NET_INET_ADDRESS_H

#include <string>
#include <samurai/io/net/socketevent.h>

// FIXME: std::string-ify this class!

namespace Samurai {
	namespace IO {
		namespace Net {
			namespace DNS {
				class Resolver;
			}
			struct __InternalAddress;
			
			/**
			 * This represents an IP address, both
			 * IPv4 and IPv6 is supported.
			 */
			class InetAddress : public ResolveEventHandler
			{
				public:
					enum Version { Unspecified, IPv4, IPv6 };
					enum ResolveState { Unresolved, Resolving, ResolveError, Resolved };
					
				public:
					InetAddress();
					InetAddress(enum Version ip_version);
					InetAddress(const std::string& host, enum Version ip_version = Unspecified);
					InetAddress(const InetAddress& address);
					InetAddress(const InetAddress* address);
					virtual ~InetAddress();
					
					/**
					 * Set a RAW address.
					 */
					bool setRawAddress(void* data, size_t length, enum Version ip_version);
					
					/**
					 * Returns true if this is a valid (and resolved) IP address.
					 * This can be either IPv4 or IPv6.
					 */
					virtual bool isValid();
					
					/**
					 * Returns true if this if this is a valid multicast address.
					 * For IPv4: this is 224.0.0.0-239.255.255.255
					 * For IPv6: this is ff00::/8
					 */
					virtual bool isMulticast();
					
					/**
					 * Returns true if this if this is a private address.
					 * For IPv4: is 10.0.0.0/8, 172.16.0.0/20 or 192.168.0.0/16
					 * For IPv6: this returns false
					 */
					virtual bool isPrivate();
					
					/**
					 * Returns true if this if this is a local loopback address.
					 * For IPv4: is 127.0.0.0/8
					 * For IPv6: is ::1
					 */
					virtual bool isLoopback() const;
					
					virtual const char* getAddress() const;
					virtual const char* toString() const;
					
					enum Version getType() const;
					
					bool operator==(const InetAddress&);
					bool operator!=(const InetAddress&);
					InetAddress& operator=(const std::string& str);
					InetAddress& operator=(const InetAddress&);

					bool isResolved();

					std::string getHostname() const { return hostname; }
				
					/**
					 * Start lookup hostname
					 */
					void lookup(ResolveEventHandler* eventHandler = 0);
					
					
				protected:
					void EventHostFound(InetAddress* addr);
					void EventHostError(enum Samurai::IO::Net::DNS::Resolver::Error error);
					
				protected:
					static bool stringToAddress(enum Version, char* address, struct __InternalAddress*);
					
					
				protected:
					enum Version version;
					struct __InternalAddress* data;
					std::string hostname;
					Samurai::IO::Net::DNS::Resolver* resolver;
					enum ResolveState resolveState;
					ResolveEventHandler* dnsevent;
					mutable char* string;
					
				friend class InetSocketAddress;
				friend class SocketBase;
			};
		}
	}
}

#endif // HAVE_SYSTEM_NET_INET_ADDRESS_H

