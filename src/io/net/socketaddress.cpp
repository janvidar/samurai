/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/net/socketglue.h>

Samurai::IO::Net::InetSocketAddress::InetSocketAddress()
{
	data = 0;
	addr = new Samurai::IO::Net::InetAddress();
	port = 0;
	string = 0;
}


Samurai::IO::Net::InetSocketAddress::InetSocketAddress(const Samurai::IO::Net::InetSocketAddress& isa) : Samurai::IO::Net::SocketAddress()
{
	data = 0;
	addr = new Samurai::IO::Net::InetAddress(isa.addr);
	port = isa.port;
	string = 0;
}


Samurai::IO::Net::InetSocketAddress::InetSocketAddress(const Samurai::IO::Net::InetSocketAddress* isa)
{
	data = 0;
	addr = new Samurai::IO::Net::InetAddress(isa->addr);
	port = isa->port;
	string = 0;
}


Samurai::IO::Net::InetSocketAddress::InetSocketAddress(const Samurai::IO::Net::InetAddress& addr_, uint16_t port_)
{
	data = 0;
	addr = new Samurai::IO::Net::InetAddress(addr_);
	port = port_;
	string = 0;
}


Samurai::IO::Net::InetSocketAddress::InetSocketAddress(uint16_t port_)
{
	data = 0;
	addr = new Samurai::IO::Net::InetAddress("0.0.0.0");
	port = port_;
	string = 0;
}


Samurai::IO::Net::InetSocketAddress::InetSocketAddress(const char* ip, uint16_t port_, enum Samurai::IO::Net::InetAddress::Version  version)
{
	data = 0;
	addr = new Samurai::IO::Net::InetAddress(ip, version);
	port = port_;
	string = 0;
}


Samurai::IO::Net::InetSocketAddress::~InetSocketAddress()
{
	delete addr;
	delete data;
	delete[] string;
}


Samurai::IO::Net::InetAddress* Samurai::IO::Net::InetSocketAddress::getAddress() const
{
	return addr;
}


uint16_t     Samurai::IO::Net::InetSocketAddress::getPort()
{
	return port;
}


const char*  Samurai::IO::Net::InetSocketAddress::toString()
{
	if (string) return string;
	
	string = new char[INET6_ADDRSTRLEN+9];
	memset(string, 0, INET6_ADDRSTRLEN+9);

	if (addr->getType() == Samurai::IO::Net::InetAddress::IPv4) {
		strcat(string, addr->toString());
		strcat(string, ":");
		strcat(string, quickdc_itoa(port, 10));
	} else if (addr->getType() == Samurai::IO::Net::InetAddress::IPv6) {
		strcat(string, "[");
		strcat(string, addr->toString());
		strcat(string, "]:");
		strcat(string, quickdc_itoa(port, 10));
	} else {
		return 0;
	}

	return string;
}


bool Samurai::IO::Net::InetSocketAddress::isLinkLocal() {
	if (addr->getType() == Samurai::IO::Net::InetAddress::IPv4) {
		return (strncmp(toString(), "127.", 4) == 0);
	} else if (addr->getType() == Samurai::IO::Net::InetAddress::IPv6) {
		return (strncmp(toString(), "[::1]:", 6) == 0);
	}
	return false;
}


int Samurai::IO::Net::InetSocketAddress::getSockAddrFamily()
{
	if (addr->getType() == Samurai::IO::Net::InetAddress::IPv4) {
		return AF_INET;
	} else if (addr->getType() == Samurai::IO::Net::InetAddress::IPv6) {
		return AF_INET6;
	} else {
		return AF_UNSPEC;
	}
}


struct sockaddr* Samurai::IO::Net::InetSocketAddress::getSockAddr()
{
	if (data) return data;

	if (addr->getType() == Samurai::IO::Net::InetAddress::IPv4) {
		struct sockaddr_in* sa = new struct sockaddr_in();
		sa->sin_family = AF_INET;
		sa->sin_port = htons(port);
		memcpy(&sa->sin_addr, (void*) &addr->data->internal.in, sizeof(struct in_addr));
		data = (sockaddr*) sa;
		
	} else if (addr->getType() == Samurai::IO::Net::InetAddress::IPv6) {
		struct sockaddr_in6* sa = new struct sockaddr_in6();
		sa->sin6_family = AF_INET6;
		sa->sin6_port = htons(port);
		sa->sin6_flowinfo = 0; // FIXME: ?
		memcpy(&sa->sin6_addr, (void*) &addr->data->internal.in6, sizeof(struct in6_addr));
		sa->sin6_scope_id = 0; // FIXME: ?
		data = (sockaddr*) sa;
	} else {
		return 0;
	}

	return data;
}



size_t Samurai::IO::Net::InetSocketAddress::getSockAddrSize()
{
	if (addr->getType() == Samurai::IO::Net::InetAddress::IPv4) {
		return sizeof(struct sockaddr_in);
	} else if (addr->getType() == Samurai::IO::Net::InetAddress::IPv6) {
		return sizeof(struct sockaddr_in6);
	} else {
		return 0;
	}
}

void Samurai::IO::Net::InetSocketAddress::setRawSocketAddress(void* sockaddr_data, size_t sockaddr_len, uint16_t port_, enum Samurai::IO::Net::InetAddress::Version version_)
{
	if (string) { delete[] string; string = 0; }
	if (addr) delete addr;
	addr = new InetAddress();
	addr->setRawAddress(sockaddr_data, sockaddr_len, version_);
	port = port_;
}



