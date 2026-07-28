/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/socketglue.h>
#include <samurai/io/net/bandwidth.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/datagram.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/buffer.h>

#ifdef SAMURAI_POSIX
#include <signal.h>
#include <sys/wait.h>
#endif

#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <memory>

#define MAX_BUF_SIZE 65536

Samurai::IO::Net::DatagramPacket::DatagramPacket() : socket(nullptr) {
	buffer = std::make_unique<Samurai::IO::Buffer>(MAX_BUF_SIZE);
}

Samurai::IO::Net::DatagramPacket::DatagramPacket(const uint8_t* buf, size_t len) : socket(nullptr) {
	buffer = std::make_unique<Samurai::IO::Buffer>(len);
	buffer->append((char*) buf, len);
}

Samurai::IO::Net::DatagramPacket::DatagramPacket(const char* buf) : socket(nullptr) {
	buffer = std::make_unique<Samurai::IO::Buffer>(strlen(buf));
	buffer->append(buf);

}

Samurai::IO::Net::DatagramPacket::DatagramPacket(Samurai::IO::Buffer* buf) : socket(nullptr) {
	buffer = std::make_unique<Samurai::IO::Buffer>(buf);
}

void Samurai::IO::Net::DatagramPacket::setData(const uint8_t* buf, size_t len) {
	buffer->clear();
	buffer->append((char*) buf, len);
}

void Samurai::IO::Net::DatagramPacket::setAddress(Samurai::IO::Net::SocketAddress* addr_)
{
	addr.reset();

	/*
	 * Only an InetSocketAddress can be copied here, and the cast has to be
	 * checked before the result is used: the pointer-taking constructor
	 * dereferences its argument, so a failed cast would go straight through it.
	 */
	if (InetSocketAddress* isa = dynamic_cast<InetSocketAddress*>(addr_))
		addr = std::make_unique<InetSocketAddress>(*isa);
}

Samurai::IO::Net::SocketAddress* Samurai::IO::Net::DatagramPacket::getAddress()
{
	return addr.get();
}

void Samurai::IO::Net::DatagramPacket::clear() {
	buffer->clear();
}

Samurai::IO::Net::DatagramPacket::~DatagramPacket() = default;

size_t Samurai::IO::Net::DatagramPacket::size() {
	return buffer->size();
}

Samurai::IO::Buffer* Samurai::IO::Net::DatagramPacket::getBuffer() {
	return buffer.get();
}

Samurai::IO::Net::DatagramSocket::DatagramSocket(DatagramEventHandler* eh, InetAddress::Version version) : SocketBase(SocketType::Datagram), eventHandler(eh), myPacket(nullptr)
{
	int af = (version == InetAddress::Version::IPv4 ? AF_INET : version == InetAddress::Version::IPv6 ? AF_INET6 : AF_UNSPEC);
	createDescriptor(af);
}

/*
 * NOTE: there is no useful datagram socket to build without an address family,
 * so this leaves the socket unbound and says so; listen() and send() refuse an
 * INVALID_SOCKET.
 */
Samurai::IO::Net::DatagramSocket::DatagramSocket() : SocketBase(SocketType::Datagram), eventHandler(nullptr), myPacket(nullptr) {
	QERR("DatagramSocket: default-constructed sockets have no address family "
	     "and cannot be used; construct with an address, port or version");
}

Samurai::IO::Net::DatagramSocket::DatagramSocket(Samurai::IO::Net::DatagramEventHandler* eh, const Samurai::IO::Net::SocketAddress& bindAddr)
	: SocketBase(bindAddr, SocketType::Datagram), eventHandler(eh), myPacket(nullptr)
{
	internal_create();
}

Samurai::IO::Net::DatagramSocket::DatagramSocket(DatagramEventHandler* eh, const Samurai::IO::Net::InetAddress& bindaddr, uint16_t bindport)
	: SocketBase(bindaddr, bindport, SocketType::Datagram), eventHandler(eh), myPacket(nullptr)
{
	internal_create();
}

Samurai::IO::Net::DatagramSocket::DatagramSocket(DatagramEventHandler* eh, uint16_t bindport)
	: SocketBase(Samurai::IO::Net::InetAddress("0.0.0.0"), bindport, SocketType::Datagram), eventHandler(eh), myPacket(nullptr)
{
	internal_create();
}


void Samurai::IO::Net::DatagramSocket::internal_create() {
	if (addr) createDescriptor(addr->getSockAddrFamily());
}

/* Registering with the monitor needs shared_from_this(), so it cannot happen
   in a constructor. */
void Samurai::IO::Net::DatagramSocket::initialize() {
	if (sd != INVALID_SOCKET)
		setMonitor(Samurai::IO::Net::SocketMonitor::Triggers::Read);
}


Samurai::IO::Net::DatagramSocket::~DatagramSocket() {
	disableMonitor();
	close();
}

void Samurai::IO::Net::DatagramSocket::setEventHandler(DatagramEventHandler* eh) {
	eventHandler = eh;
}

bool Samurai::IO::Net::DatagramSocket::listen() {
	if (!addr || sd == INVALID_SOCKET) return false;
	if (!setReuseAddress(true)) return false;
	if (!setNonBlocking(true)) return false;
	if (!bind(addr.get())) return false;
	return true;
}


int Samurai::IO::Net::DatagramSocket::send(DatagramPacket* packet) {
	if (!packet || !packet->addr) return -1;


	/* The packet's own buffer already holds the bytes contiguously. */
	size_t length = packet->buffer->size();
	if (length > MAX_BUF_SIZE) length = MAX_BUF_SIZE;
	if (!length) return 0;

	const char* data = packet->buffer->ptr();
	if (!data) return -1;

	struct sockaddr* sa = packet->addr->getSockAddr();
	size_t sa_len = packet->addr->getSockAddrSize();

	ssize_t ret = ::sendto(sd, Samurai::IO::Net::sendto_arg(data), length, SAMURAI_SENDFLAGS, (sockaddr*) sa, sa_len);

	if (ret == -1) {
		if (Samurai::IO::Net::net_error() == EAGAIN || Samurai::IO::Net::net_error() == EWOULDBLOCK || Samurai::IO::Net::net_error() == EINTR) {
			return 0;
		} else {
			if (eventHandler) eventHandler->EventDatagramError(this, strerror(Samurai::IO::Net::net_error()));
			return -1;
		}
	}

	/* NOTE: Buffer::pop() copies without consuming, so what was sent has to be
	   removed explicitly or the next send() resends it. */
	packet->buffer->remove((size_t) ret);

	if (bandwidthManager) bandwidthManager->dataSendUDP((size_t) ret);
	return ret;
}

int Samurai::IO::Net::DatagramSocket::read(DatagramPacket* packet) {
	size_t length = sizeof(readbuf);
	uint8_t* data = readbuf;

	/* sockaddr_storage, so the source address of an Version::IPv6 datagram fits too. */
	struct sockaddr_storage sa;
	socklen_t sl = sizeof(sa);
	memset(&sa, 0, sizeof(sa));

	const ssize_t status = ::recvfrom(sd, (char*) data, length, 0, (sockaddr*) &sa, &sl);

	if (status == -1) {
		QERR("recvfrom err: %s", strerror(Samurai::IO::Net::net_error()));
		return -1;
	}

	if (status == 0) {
		packet->clear();
		return 0;
	}

	InetSocketAddress taddr;
	bool have_addr = false;

	if (sa.ss_family == AF_INET) {
		struct sockaddr_in* sin = (struct sockaddr_in*) &sa;
		taddr.setRawSocketAddress(&sin->sin_addr, sizeof(sin->sin_addr),
		                          ntohs(sin->sin_port), Samurai::IO::Net::InetAddress::Version::IPv4);
		have_addr = true;
	} else if (sa.ss_family == AF_INET6) {
		struct sockaddr_in6* sin6 = (struct sockaddr_in6*) &sa;
		taddr.setRawSocketAddress(&sin6->sin6_addr, sizeof(sin6->sin6_addr),
		                          ntohs(sin6->sin6_port), Samurai::IO::Net::InetAddress::Version::IPv6);
		have_addr = true;
	}

	packet->setData(data, (size_t) status);
	if (have_addr) packet->setAddress(&taddr);

	if (bandwidthManager) bandwidthManager->dataRecvUDP((size_t) status);
	return (int) status;
}


void Samurai::IO::Net::DatagramSocket::handleMonitorEvent(Samurai::IO::Net::SocketMonitor::Triggers trig)
{
	if (any(trig & Samurai::IO::Net::SocketMonitor::Triggers::Read))
		internal_canRead();

	if (any(trig & (Samurai::IO::Net::SocketMonitor::Triggers::Error | Samurai::IO::Net::SocketMonitor::Triggers::Close)))
		internal_error();
}


void Samurai::IO::Net::DatagramSocket::internal_canRead() {
	if (!myPacket) myPacket = std::make_unique<DatagramPacket>();
	
	switch (read(myPacket.get())) {
		case -1:
			if (eventHandler) eventHandler->EventDatagramError(this, strerror(Samurai::IO::Net::net_error()));
			break;
		case 0:
			break;
		default:
			if (eventHandler) eventHandler->EventGotDatagram(this, myPacket.get());
			break;
	}
}

void Samurai::IO::Net::DatagramSocket::internal_error() {
	if (eventHandler) eventHandler->EventDatagramError(this, strerror(Samurai::IO::Net::net_error()));
	disableMonitor();
}

