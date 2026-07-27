/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/error.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/bandwidth.h>


Samurai::IO::Net::SocketBase::SocketBase(const Samurai::IO::Net::SocketAddress& addr_, enum SocketType type_) : sd(INVALID_SOCKET), addr(0), state(Connected), ia(0),  monitor_trigger(0), monitored(false), type(type_) {

	if (dynamic_cast<Samurai::IO::Net::InetSocketAddress*>(const_cast<Samurai::IO::Net::SocketAddress*>(&addr_)))
		addr = new Samurai::IO::Net::InetSocketAddress((Samurai::IO::Net::InetSocketAddress*)dynamic_cast<Samurai::IO::Net::InetSocketAddress*>(const_cast<Samurai::IO::Net::SocketAddress*>(&addr_)));
	
	bandwidthManager = Samurai::IO::Net::BandwidthManager::getInstance();
}


Samurai::IO::Net::SocketBase::SocketBase(socket_t sd_, const Samurai::IO::Net::SocketAddress& addr_, enum SocketType type_) : sd(sd_), addr(0), state(Connected), ia(0),  monitor_trigger(0), monitored(false), type(type_)
{
	if (dynamic_cast<Samurai::IO::Net::InetSocketAddress*>(const_cast<Samurai::IO::Net::SocketAddress*>(&addr_)))
		addr = new Samurai::IO::Net::InetSocketAddress((Samurai::IO::Net::InetSocketAddress*)dynamic_cast<Samurai::IO::Net::InetSocketAddress*>(const_cast<Samurai::IO::Net::SocketAddress*>(&addr_)));

	bandwidthManager = Samurai::IO::Net::BandwidthManager::getInstance();
}

Samurai::IO::Net::SocketBase::SocketBase(const Samurai::IO::Net::InetAddress& addr_, uint16_t port_, enum SocketType type_) : sd(INVALID_SOCKET), addr(0), state(Connected), ia(0),  monitor_trigger(0), monitored(false), type(type_) {
	addr = new Samurai::IO::Net::InetSocketAddress(addr_, port_);

	bandwidthManager = Samurai::IO::Net::BandwidthManager::getInstance();
}


Samurai::IO::Net::SocketBase::SocketBase(enum SocketType type_) : sd(INVALID_SOCKET), addr(0), state(Disconnected), ia(0), monitor_trigger(0), monitored(false), type(type_)
{
	bandwidthManager = Samurai::IO::Net::BandwidthManager::getInstance();
}

Samurai::IO::Net::SocketBase::~SocketBase() {
	disableMonitor();
	delete addr;
	delete ia;
}

const Samurai::IO::Net::InetAddress* Samurai::IO::Net::SocketBase::getLocalAddress() const {
	/*
	 * NOTE: the IPv6 branch passed sizeof(struct in_addr) - four bytes - for a
	 * sixteen byte address, and setRawAddress() rejects anything shorter than
	 * the family requires, so this silently returned an all-zero address for
	 * every IPv6 socket. It also chose the branch from the *remote* address's
	 * family rather than from what getsockname() actually returned.
	 */
	if (sd == INVALID_SOCKET) return 0;

	struct sockaddr_storage localaddr;
	socklen_t len = sizeof(localaddr);
	memset(&localaddr, 0, sizeof(localaddr));

	if (getsockname(sd, (sockaddr*) &localaddr, &len) != 0) return 0;

	if (!ia) ia = new Samurai::IO::Net::InetAddress();

	if (localaddr.ss_family == AF_INET) {
		struct sockaddr_in* sin = (struct sockaddr_in*) &localaddr;
		if (!ia->setRawAddress(&sin->sin_addr, sizeof(sin->sin_addr),
		                       Samurai::IO::Net::InetAddress::IPv4)) return 0;
		return ia;
	}

	if (localaddr.ss_family == AF_INET6) {
		struct sockaddr_in6* sin6 = (struct sockaddr_in6*) &localaddr;
		if (!ia->setRawAddress(&sin6->sin6_addr, sizeof(sin6->sin6_addr),
		                       Samurai::IO::Net::InetAddress::IPv6)) return 0;
		return ia;
	}

	return 0;
}

bool Samurai::IO::Net::SocketBase::bind(Samurai::IO::Net::SocketAddress* sa) {
	std::error_code ec;
	return bind(sa, ec);
}


bool Samurai::IO::Net::SocketBase::bind(Samurai::IO::Net::SocketAddress* sa, std::error_code& ec) {
	ec.clear();

	InetSocketAddress* isa = dynamic_cast<InetSocketAddress*>(sa);
	if (isa && sd != INVALID_SOCKET) {
		if (isa->getAddress()->getType() == Samurai::IO::Net::InetAddress::IPv4) {
			struct sockaddr_in localaddr;
			socklen_t len = sizeof(struct sockaddr_in);
			memset(&localaddr, 0, len);
			localaddr.sin_family = AF_INET;
			localaddr.sin_port   = htons((int) isa->getPort());
			memcpy(&localaddr.sin_addr, (void*) &isa->getAddress()->data->internal.in, sizeof(struct in_addr));
			
			int ret = ::bind(sd, (sockaddr*) &localaddr, len);
			if (ret == SOCKET_ERROR)
			{
				ec = Samurai::system_error(NETERROR);
				QDBG("Bind on IPv4 failed. Error %d: %s", NETERROR, strerror(NETERROR));
				return false;
			}
			return true;
			
		} if (isa->getAddress()->getType() == Samurai::IO::Net::InetAddress::IPv6) {
			struct sockaddr_in6 localaddr;
			socklen_t len = sizeof(struct sockaddr_in6);
			memset(&localaddr, 0, len);
			localaddr.sin6_family = AF_INET6;
			localaddr.sin6_port = htons(isa->getPort());
			memcpy(&localaddr.sin6_addr, (void*) &isa->getAddress()->data->internal.in6, sizeof(struct in6_addr));
			int ret = ::bind(sd, (sockaddr*) &localaddr, len);
			if (ret == SOCKET_ERROR)
			{
				ec = Samurai::system_error(NETERROR);
				QDBG("Bind on IPv6 failed. Error %d: %s", NETERROR, strerror(NETERROR));
				return false;
			}
			return true;
			
		}
	}

	ec = Samurai::system_error(EINVAL);
	return false;
}


uint16_t Samurai::IO::Net::SocketBase::getLocalPort() const {
	if (state != Connected && state != Connecting && state != Listening) return 0;
	sockaddr_in localaddr;
	socklen_t len = sizeof(localaddr);
	if (getsockname(sd, (sockaddr*) &localaddr, &len) == 0)
		return ntohs(localaddr.sin_port);
	return 0;
}


const Samurai::IO::Net::InetAddress* Samurai::IO::Net::SocketBase::getAddress() const {
	InetSocketAddress* isa = dynamic_cast<InetSocketAddress*>(addr);
	if (isa) {
		if (ia) delete ia;
		ia = new Samurai::IO::Net::InetAddress(isa->getAddress());
		return ia;
	}
	return 0;
	
}


uint16_t Samurai::IO::Net::SocketBase::getPort() const {
	InetSocketAddress* isa = dynamic_cast<InetSocketAddress*>(addr);
	return  isa ? isa->getPort() : 0;
}


bool Samurai::IO::Net::SocketBase::setKeepAlive(bool toggle) {
	std::error_code ec;
	return setKeepAlive(toggle, ec);
}


bool Samurai::IO::Net::SocketBase::setKeepAlive(bool toggle, std::error_code& ec) {
	ec.clear();
	int value = (toggle) ? 1 : 0;
	if (SAMURAI_SETSOCKOPT(sd, SOL_SOCKET, SO_KEEPALIVE, &value, sizeof(value)) == SOCKET_ERROR) {
		ec = Samurai::system_error(NETERROR);
		return false;
	}
	return true;
}


bool Samurai::IO::Net::SocketBase::setNonBlocking(bool toggle) {
	std::error_code ec;
	return setNonBlocking(toggle, ec);
}


bool Samurai::IO::Net::SocketBase::setNonBlocking(bool toggle, std::error_code& ec) {
	ec.clear();

	int ret = SOCKET_ERROR;
#ifdef SAMURAI_POSIX
#ifdef SAMURAI_OS_SOLARIS
	ret = fcntl(sd, F_GETFL, 0);
	if (ret == SOCKET_ERROR)
		return false;
	
	if (toggle)
		ret |= O_NONBLOCK;
	else
		ret &= ~O_NONBLOCK;
	return fcntl(sd, F_SETFL, ret) != SOCKET_ERROR;
#else
	int on = toggle ? 1 : 0;
	ret = ioctl(sd, FIONBIO, &on);
	if (ret == SOCKET_ERROR) { ec = Samurai::system_error(NETERROR); return false; }
	return true;
#endif
#endif

#ifdef SAMURAI_WINSOCK
	u_long on = toggle ? 1 : 0;
	ret = ioctlsocket(sd, FIONBIO, &on);
	if (ret == SOCKET_ERROR) {
		QERR("ERROR: Setting socket to %s mode failed ", toggle ? "non-blocking" : "blocking");
		return false;
	}
	return true;
#endif
}

bool Samurai::IO::Net::SocketBase::setReuseAddress(bool toggle) {
	std::error_code ec;
	return setReuseAddress(toggle, ec);
}


bool Samurai::IO::Net::SocketBase::setReuseAddress(bool toggle, std::error_code& ec) {
	ec.clear();
	int on = toggle ? 1 : 0;
	if (SAMURAI_SETSOCKOPT(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == SOCKET_ERROR)
	{
		ec = Samurai::system_error(NETERROR);
		QERR("ERROR: setReuseAddress to %s failed", toggle ? "ON" : "OFF");
		return false;
	}
	return true;
}

bool Samurai::IO::Net::SocketBase::setReusePort(bool toggle) {
	std::error_code ec;
	return setReusePort(toggle, ec);
}


bool Samurai::IO::Net::SocketBase::setReusePort(bool toggle, std::error_code& ec) {
	ec.clear();
#ifndef SO_REUSEPORT
	(void) toggle;
	return true;
#else
        int on = toggle ? 1 : 0;
        if (SAMURAI_SETSOCKOPT(sd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)) == SOCKET_ERROR)
        {
                ec = Samurai::system_error(NETERROR);
                QERR("ERROR: setReusePort to %s failed", toggle ? "ON" : "OFF");
                return false;
        }
        return true;
#endif
}


bool Samurai::IO::Net::SocketBase::setSendBufferSize(size_t size) {
	std::error_code ec;
	return setSendBufferSize(size, ec);
}


bool Samurai::IO::Net::SocketBase::setSendBufferSize(size_t size, std::error_code& ec) {
	ec.clear();

	/* NOTE: this passed &size, a size_t*, where the kernel expects an int.
	   It happened to work on little endian and was wrong on big endian. */
	int value = (int) size;
	if (SAMURAI_SETSOCKOPT(sd, SOL_SOCKET, SO_SNDBUF, &value, sizeof(value)) == SOCKET_ERROR) {
		ec = Samurai::system_error(NETERROR);
		return false;
	}
	return true;
}

size_t Samurai::IO::Net::SocketBase::getSendBufferSize() const {
	int value = 0;
	socklen_t sz = sizeof(value);
	if (SAMURAI_GETSOCKOPT(sd, SOL_SOCKET, SO_SNDBUF, &value, &sz) == SOCKET_ERROR) {
		QERR("ERROR: getsockopt failed");
		return 0;
	}
	return (size_t) (value < 0 ? 0 : value);
}

bool Samurai::IO::Net::SocketBase::setReceiveBufferSize(size_t size) {
	std::error_code ec;
	return setReceiveBufferSize(size, ec);
}


bool Samurai::IO::Net::SocketBase::setReceiveBufferSize(size_t size, std::error_code& ec) {
	ec.clear();

	/* NOTE: this passed &size, a size_t*, where the kernel expects an int.
	   It happened to work on little endian and was wrong on big endian. */
	int value = (int) size;
	if (SAMURAI_SETSOCKOPT(sd, SOL_SOCKET, SO_RCVBUF, &value, sizeof(value)) == SOCKET_ERROR) {
		ec = Samurai::system_error(NETERROR);
		return false;
	}
	return true;
}

size_t Samurai::IO::Net::SocketBase::getReceiveBufferSize() const {
	int value = 0;
	socklen_t sz = sizeof(value);
	if (SAMURAI_GETSOCKOPT(sd, SOL_SOCKET, SO_RCVBUF, &value, &sz) == SOCKET_ERROR) {
		QERR("ERROR: getsockopt failed");
		return 0;
	}
	return (size_t) (value < 0 ? 0 : value);
}

/* NOTE: IP_TTL is meaningless on an IPv6 socket; the hop limit lives behind
   IPV6_UNICAST_HOPS. Both of these used IP_TTL unconditionally. */
bool Samurai::IO::Net::SocketBase::isIPv6() const
{
	InetSocketAddress* isa = dynamic_cast<InetSocketAddress*>(addr);
	return isa && isa->getAddress() &&
	       isa->getAddress()->getType() == Samurai::IO::Net::InetAddress::IPv6;
}


uint8_t Samurai::IO::Net::SocketBase::getTimeToLive() const
{
	int value = 0;
	socklen_t sz = sizeof(value);
	int ret = isIPv6()
		? SAMURAI_GETSOCKOPT(sd, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &value, &sz)
		: SAMURAI_GETSOCKOPT(sd, IPPROTO_IP, IP_TTL, &value, &sz);
	if (ret == SOCKET_ERROR) {
		QERR("ERROR: getsockopt failed");
	}
	return (uint8_t) value;
}

bool Samurai::IO::Net::SocketBase::setTimeToLive(uint8_t ttl)
{
	std::error_code ec;
	return setTimeToLive(ttl, ec);
}


bool Samurai::IO::Net::SocketBase::setTimeToLive(uint8_t ttl, std::error_code& ec)
{
	ec.clear();
	int value = ttl;

	const int level  = isIPv6() ? IPPROTO_IPV6 : IPPROTO_IP;
	const int option = isIPv6() ? IPV6_UNICAST_HOPS : IP_TTL;

	if (SAMURAI_SETSOCKOPT(sd, level, option, &value, sizeof(value)) == SOCKET_ERROR) {
		ec = Samurai::system_error(NETERROR);
		return false;
	}
	return true;
}


void Samurai::IO::Net::SocketBase::close() {
	if (sd != INVALID_SOCKET)
	{
		disableMonitor();

#ifndef SAMURAI_WINSOCK
		shutdown(sd, SHUT_RDWR);
#else
		shutdown(sd, SD_BOTH);
#endif
		
		SOCKET_CLOSE(sd);
		sd = INVALID_SOCKET;
	}
}

void Samurai::IO::Net::SocketBase::setMonitor(int trigger)
{
	monitor_trigger = trigger;
	if (monitored)
	{
		SocketMonitor::getInstance()->modify(this);
	} else {
		SocketMonitor::getInstance()->add(this);
		monitored = true;
	}
}

void Samurai::IO::Net::SocketBase::disableMonitor()
{
	if (monitored)
	{
		SocketMonitor::getInstance()->remove(this);
		monitored = false;
	}
}

bool Samurai::IO::Net::SocketBase::createDescriptor(int af)
{
	if (type == Stream)
	{
		sd = ::socket(af, SOCK_STREAM, IPPROTO_TCP);
	}
	else
	{
		sd = ::socket(af, SOCK_DGRAM,  IPPROTO_UDP);
	}
	
	if (sd == INVALID_SOCKET)
	{
		QERR("Unable to create socket: %s (%d)", strerror(NETERROR), NETERROR);
		return false;
	}
	return true;
}

void Samurai::IO::Net::SocketBase::setState(enum SocketState newState)
{
	state = newState;
}


bool Samurai::IO::Net::SocketBase::setIPv6Only(bool toggle)
{
	std::error_code ec;
	return setIPv6Only(toggle, ec);
}


bool Samurai::IO::Net::SocketBase::setIPv6Only(bool toggle, std::error_code& ec)
{
	ec.clear();

#ifdef IPV6_V6ONLY
	int on = toggle ? 1 : 0;
	if (SAMURAI_SETSOCKOPT(sd, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on)) == SOCKET_ERROR)
	{
		ec = Samurai::system_error(NETERROR);
		return false;
	}
	return true;
#else
	(void) toggle;
	ec = Samurai::system_error(ENOPROTOOPT);
	return false;
#endif
}
