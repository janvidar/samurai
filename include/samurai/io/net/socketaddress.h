/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SYSTEM_NET_SOCKET_ADDRESS_H
#define HAVE_SYSTEM_NET_SOCKET_ADDRESS_H

#include <samurai/io/net/inetaddress.h>

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

					virtual const char*  toString() = 0;
					
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
					
					void setRawSocketAddress(void* sockaddr_data, size_t sockaddr_len, uint16_t port, enum Samurai::IO::Net::InetAddress::Version version);
					InetAddress* getAddress() const;
					uint16_t     getPort();
					const char*  toString();
					bool isLinkLocal();

					int getSockAddrFamily();
					struct sockaddr* getSockAddr();
					size_t getSockAddrSize();
					
				protected:
					struct sockaddr* data;
					InetAddress* addr;
					uint16_t port;
					char* string;
			};
			
#if 0
			/**
			 * FIXME: Use File class as member.
			 */
			class UnixSocketAddress : public SocketAddress {
				public:
					UnixSocketAddress(const char* filename);
					
					const char*  getFileName();
					const char*  toString();
			};
			
#endif // 0
			
		}
	}
}

#endif // HAVE_SYSTEM_NET_SOCKET_ADDRESS_H
