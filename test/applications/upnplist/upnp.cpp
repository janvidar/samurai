/*
 * Copyright (C) 2009 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include "upnp.h"
#include <samurai/samurai.h>
#include <samurai/os.h>

UPnP::PacketHeader::PacketHeader(const UPnP::PacketHeader& header)
	: m_name(header.m_name)
	, m_value(header.m_value)
{
}

UPnP::PacketHeader::PacketHeader(const std::string& name, const std::string& value)
	: m_name(name)
	, m_value(value)
{
	
}

UPnP::PacketHeader::PacketHeader(const std::string& name, int value)
	: m_name(name)
	, m_value("")
{
	m_value += quickdc_itoa(value, 10);
}

	
std::string UPnP::PacketHeader::getName() const
{
	return m_name;
}

std::string UPnP::PacketHeader::getValue() const
{
	return m_value;
}

std::string UPnP::PacketHeader::toString()
{
	std::string ret = m_name;
	ret += ": ";
	ret += m_value;
	return ret;
}


UPnP::Packet::Packet(enum Type type)
	: m_type(type)
{
}

UPnP::Packet::~Packet()
{
}

void UPnP::Packet::addHeader(const UPnP::PacketHeader& header)
{
	m_headers.push_back(header);
}

bool UPnP::Packet::copy(Samurai::IO::Net::DatagramPacket& dgram)
{
	std::string message;
	switch (m_type)
	{
		case Notify:   message = "NOTIFY * HTTP/1.1\r\n";   break;
		case Search:   message = "M-SEARCH * HTTP/1.1\r\n"; break;
		case Response: message = "HTTP/1.1 200 OK\r\n";     break;
	}
	
	for (std::vector<UPnP::PacketHeader>::iterator it = m_headers.begin(); it != m_headers.end(); ++it)
	{
		message += (*it).toString();
		message += "\r\n";
	}
	
	// Add the SERVER or USER-AGENT header
	if (m_type == Search) message += "USER-AGENT: ";
	else                  message += "SERVER: ";
	message += Samurai::OS::getName();
	message += "/";
	message += Samurai::OS::getVersion();
	message += " UPnP/1.1 ";
	message += "Samurai/1.0"; // FIXME
	message += "\r\n";

	// Add an extra newline
	message += "\r\n";

	dgram.setData((uint8_t*) message.c_str(), message.size());

	return true;
}

