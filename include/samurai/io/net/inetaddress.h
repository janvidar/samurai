/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SYSTEM_NET_INET_ADDRESS_H
#define HAVE_SYSTEM_NET_INET_ADDRESS_H

#include <stdint.h>
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
			 * Version::IPv4 and Version::IPv6 is supported.
			 */
			class InetAddress : public ResolveEventHandler
			{
				public:
					enum class Version { Unspecified, IPv4, IPv6 };
					enum class ResolveState { Unresolved, Resolving, ResolveError, Resolved };
					
				public:
					InetAddress();
					InetAddress(Version ip_version);
					InetAddress(const std::string& host, Version ip_version = Version::Unspecified);
					InetAddress(const InetAddress& address);
					InetAddress(const InetAddress* address);
					~InetAddress() override;
					
					/**
					 * Set a RAW address.
					 */
					bool setRawAddress(void* data, size_t length, Version ip_version);
					
					/**
					 * Returns true if this is a valid (and resolved) IP address.
					 * This can be either Version::IPv4 or Version::IPv6.
					 */
					virtual bool isValid() const;
					
					/**
					 * Returns true if this if this is a valid multicast address.
					 * For Version::IPv4: this is 224.0.0.0-239.255.255.255
					 * For Version::IPv6: this is ff00::/8
					 */
					virtual bool isMulticast() const;
					
					/**
					 * Returns true if this if this is a private address.
					 * For Version::IPv4: is 10.0.0.0/8, 172.16.0.0/12 or 192.168.0.0/16
					 * For Version::IPv6: is fc00::/7
					 */
					virtual bool isPrivate() const;

					/**
					 * Returns true if this if this is a local loopback address.
					 * For Version::IPv4: is 127.0.0.0/8
					 * For Version::IPv6: is ::1
					 */
					virtual bool isLoopback() const;

					/**
					 * Returns true if this is a link local address.
					 * For Version::IPv4: is 169.254.0.0/16
					 * For Version::IPv6: is fe80::/10
					 */
					virtual bool isLinkLocal() const;
					
					/**
					 * The Version::IPv4 address as a host order integer, or 0 if this
					 * is not an Version::IPv4 address. The stored bytes are network
					 * order; read them through this rather than converting at
					 * each use, so a host order predicate cannot be handed a
					 * network order value.
					 */
					uint32_t getIPv4HostOrder() const;

					virtual std::string getAddress() const;
					virtual std::string toString() const;
					
					Version getType() const;
					
					bool operator==(const InetAddress&) const;
					InetAddress& operator=(const std::string& str);
					InetAddress& operator=(const InetAddress&);

					bool isResolved() const;

					std::string getHostname() const { return hostname; }

					/**
					 * Attach the name this address answers to.
					 *
					 * The address itself is left alone: a reverse lookup adds a
					 * name to an address that is already known, it does not
					 * replace it.
					 */
					void setHostname(const std::string& name) { hostname = name; }


					/**
					 * Start lookup hostname
					 */
					void lookup(ResolveEventHandler* eventHandler = nullptr);
					
					
				protected:
					void EventHostFound(const InetAddress* addr) override;
					void EventHostError(Samurai::IO::Net::DNS::Resolver::Error error) override;
					
				protected:
					static bool stringToAddress(Version, const char* address, struct __InternalAddress*);
					
					
				protected:
					Version version;
					/* Owning. Stays pointer-like because the X_IP* accessor macros in
					   socketglue.h dereference it. */
					std::unique_ptr<struct __InternalAddress> data;
					std::string hostname;
					std::unique_ptr<Samurai::IO::Net::DNS::Resolver> resolver;
					ResolveState resolveState;
					ResolveEventHandler* dnsevent;
					
				friend class InetSocketAddress;
				friend class SocketBase;
			};
		}
	}
}

#endif // HAVE_SYSTEM_NET_INET_ADDRESS_H

