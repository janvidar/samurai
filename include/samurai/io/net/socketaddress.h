/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SYSTEM_NET_SOCKET_ADDRESS_H
#define HAVE_SYSTEM_NET_SOCKET_ADDRESS_H

#include <samurai/io/net/inetaddress.h>
#include <vector>

struct sockaddr;

namespace Samurai {
	namespace IO {
		namespace Net {
			class InetAddress;
			struct __InternalSocketAddress;
			
			class SocketAddress {
				public:
					virtual ~SocketAddress() { }
			
					virtual int getSockAddrFamily() = 0;
					virtual struct sockaddr* getSockAddr() = 0;
					virtual size_t getSockAddrSize() = 0;

					virtual std::string  toString() = 0;
					
					virtual bool isLinkLocal() = 0;
			};

			class InetSocketAddress : public SocketAddress {
				public:
					InetSocketAddress();
					InetSocketAddress(const InetSocketAddress*);
					InetSocketAddress(const InetSocketAddress&);
					InetSocketAddress(const InetAddress& addr, uint16_t port);
					InetSocketAddress(uint16_t port);
					InetSocketAddress(const char* ip, uint16_t port, enum Samurai::IO::Net::InetAddress::Version version);
					virtual ~InetSocketAddress();

					/* Declared because the class owns 'addr': the implicit
					 * assignment would copy the pointer and release it twice. */
					InetSocketAddress& operator=(const InetSocketAddress& isa);

					void setRawSocketAddress(void* sockaddr_data, size_t sockaddr_len, uint16_t port, enum Samurai::IO::Net::InetAddress::Version version);
					InetAddress* getAddress() const;
					uint16_t     getPort();
					std::string  toString();
					bool isLinkLocal();

					int getSockAddrFamily();
					struct sockaddr* getSockAddr();
					size_t getSockAddrSize();
					
				protected:
					/*
					 * Storage for the platform sockaddr_in / sockaddr_in6 that
					 * getSockAddr() hands out. Held as bytes rather than as a
					 * 'struct sockaddr*' because the pointer was allocated as
					 * one of the two concrete types, and releasing it through
					 * the unrelated base type is undefined. Empty until
					 * getSockAddr() builds it.
					 */
					std::vector<char> data;
					InetAddress* addr;
					uint16_t port;
			};
			
		}
	}
}

#endif // HAVE_SYSTEM_NET_SOCKET_ADDRESS_H
