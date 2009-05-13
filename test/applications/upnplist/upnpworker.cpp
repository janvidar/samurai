/*
 * Copyright (C) 2009 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include "upnpworker.h"
#include <stdlib.h>

UPnP::Worker::Worker(Samurai::IO::Net::NetworkInterface* iface, const Samurai::IO::Net::InetSocketAddress& address)
	: m_socket(0)
	, m_addr(address)
{
	m_socket = new Samurai::IO::Net::MulticastSocket(this, m_addr.getPort());
	m_socket->listen();
	m_socket->setTimeToLive(5);
	m_socket->setLoopbackMode(true);
	m_socket->setInterface(iface);

	// FIXME: API: Should also accept InetSocketAddress
	if (!m_socket->join(m_addr.getAddress(), m_addr.getPort()))
	{
		delete m_socket;
		m_socket = 0;
	}
}

UPnP::Worker::~Worker()
{
	delete m_socket;
}

void UPnP::Worker::locateServices()
{
	UPnP::Packet packet(UPnP::Packet::Search);
	packet.addHeader(UPnP::PacketHeader("HOST", "239.255.255.250:1900"));
	packet.addHeader(UPnP::PacketHeader("MAN", "\"ssdp:discover\""));
	packet.addHeader(UPnP::PacketHeader("ST", "ssdp:all" )); // "ssdp:rootdevice"
	packet.addHeader(UPnP::PacketHeader("MX", 3));
//	packet.addHeader(UPnP::PacketHeader("User-Agent", "lightbulb Samurai/0.4.19"));
	send(packet);
}

void UPnP::Worker::send(UPnP::Packet& packet)
{
	Samurai::IO::Net::DatagramPacket dgram;
	packet.copy(dgram);
	
	// FIXME: API: Should not require pointer
	dgram.setAddress(&m_addr);

	// FIXME: API: Should not require pointer
	m_socket->send(&dgram);
}

void UPnP::Worker::EventGotDatagram(Samurai::IO::Net::DatagramSocket*, Samurai::IO::Net::DatagramPacket* packet)
{
	Samurai::IO::Buffer* buffer = packet->getBuffer();
	printf("Got a packet (%d bytes) from %s:\n", (int) buffer->size(), packet->getAddress() ? packet->getAddress()->toString() : "'wtf?'");
	char* buf = buffer->memdup(0, buffer->size());
	printf("[%s]\n", buf);
	free(buf);
}

void UPnP::Worker::EventDatagramError(const Samurai::IO::Net::DatagramSocket*, const char*)
{
	puts("Got an error");
}
