/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/net/socketglue.h>

Samurai::IO::Net::InetSocketAddress::InetSocketAddress()
	: port(0)
{
}


Samurai::IO::Net::InetSocketAddress::InetSocketAddress(const Samurai::IO::Net::InetSocketAddress& isa)
	: Samurai::IO::Net::SocketAddress()
	, addr(isa.addr)
	, port(isa.port)
{
}


Samurai::IO::Net::InetSocketAddress::InetSocketAddress(const Samurai::IO::Net::InetSocketAddress* isa)
	: addr(isa->addr)
	, port(isa->port)
{
}


Samurai::IO::Net::InetSocketAddress::InetSocketAddress(const Samurai::IO::Net::InetAddress& addr_, uint16_t port_)
	: addr(addr_)
	, port(port_)
{
}


Samurai::IO::Net::InetSocketAddress::InetSocketAddress(uint16_t port_)
	: addr("0.0.0.0")
	, port(port_)
{
}


Samurai::IO::Net::InetSocketAddress::InetSocketAddress(const char* ip, uint16_t port_, enum Samurai::IO::Net::InetAddress::Version  version)
	: addr(ip, version)
	, port(port_)
{
}


Samurai::IO::Net::InetSocketAddress::~InetSocketAddress() = default;


Samurai::IO::Net::InetSocketAddress& Samurai::IO::Net::InetSocketAddress::operator=(const Samurai::IO::Net::InetSocketAddress& isa)
{
	if (this == &isa) return *this;

	addr = isa.addr;
	port = isa.port;

	/* Discard the cached sockaddr rather than copying it; it is rebuilt from
	   the new address on the next getSockAddr(). */
	data.clear();
	return *this;
}


const Samurai::IO::Net::InetAddress* Samurai::IO::Net::InetSocketAddress::getAddress() const
{
	return &addr;
}


uint16_t     Samurai::IO::Net::InetSocketAddress::getPort()
{
	return port;
}


std::string  Samurai::IO::Net::InetSocketAddress::toString()
{
	const std::string address = addr.toString();
	const std::string portstr = std::to_string(port);

	if (addr.getType() == Samurai::IO::Net::InetAddress::IPv4)
		return address + ":" + portstr;

	if (addr.getType() == Samurai::IO::Net::InetAddress::IPv6)
		return "[" + address + "]:" + portstr;

	return std::string();
}


bool Samurai::IO::Net::InetSocketAddress::isLinkLocal() {
	return addr.isLinkLocal();
}


int Samurai::IO::Net::InetSocketAddress::getSockAddrFamily()
{
	if (addr.getType() == Samurai::IO::Net::InetAddress::IPv4) {
		return AF_INET;
	} else if (addr.getType() == Samurai::IO::Net::InetAddress::IPv6) {
		return AF_INET6;
	} else {
		return AF_UNSPEC;
	}
}


struct sockaddr* Samurai::IO::Net::InetSocketAddress::getSockAddr()
{
	if (!data.empty()) return reinterpret_cast<struct sockaddr*>(data.data());

	if (addr.getType() == Samurai::IO::Net::InetAddress::IPv4) {
		struct sockaddr_in sa = {};
		sa.sin_family = AF_INET;
		sa.sin_port = htons(port);
		memcpy(&sa.sin_addr, (void*) &addr.data->internal.in, sizeof(struct in_addr));
		data.resize(sizeof(sa));
		memcpy(data.data(), &sa, sizeof(sa));

	} else if (addr.getType() == Samurai::IO::Net::InetAddress::IPv6) {
		struct sockaddr_in6 sa = {};
		sa.sin6_family = AF_INET6;
		sa.sin6_port = htons(port);
		sa.sin6_flowinfo = 0; // FIXME: ?
		memcpy(&sa.sin6_addr, (void*) &addr.data->internal.in6, sizeof(struct in6_addr));
		sa.sin6_scope_id = 0; // FIXME: ?
		data.resize(sizeof(sa));
		memcpy(data.data(), &sa, sizeof(sa));
	} else {
		return nullptr;
	}

	return reinterpret_cast<struct sockaddr*>(data.data());
}



size_t Samurai::IO::Net::InetSocketAddress::getSockAddrSize()
{
	if (addr.getType() == Samurai::IO::Net::InetAddress::IPv4) {
		return sizeof(struct sockaddr_in);
	} else if (addr.getType() == Samurai::IO::Net::InetAddress::IPv6) {
		return sizeof(struct sockaddr_in6);
	} else {
		return 0;
	}
}

void Samurai::IO::Net::InetSocketAddress::setRawSocketAddress(void* sockaddr_data, size_t sockaddr_len, uint16_t port_, enum Samurai::IO::Net::InetAddress::Version version_)
{
	addr = InetAddress();
	addr.setRawAddress(sockaddr_data, sockaddr_len, version_);
	port = port_;

	/* The cached sockaddr described the previous address. */
	data.clear();
}



