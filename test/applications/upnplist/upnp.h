/*
 * Copyright (C) 2009 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_UPNP_COMMON_H
#define HAVE_SAMURAI_UPNP_COMMON_H

#include <string>
#include <vector>
#include <samurai/io/net/datagram.h>

namespace UPnP
{

class PacketHeader
{
	public:
		PacketHeader(const PacketHeader& header);
		PacketHeader(const std::string& name, const std::string& value);
		PacketHeader(const std::string& name, int value);
		
		std::string getName() const;
		std::string getValue() const;
		std::string toString();
	private:
		std::string m_name;
		std::string m_value;
};
	
class Packet
{
	public:
		enum Type { Notify, Search, Response };
		
		Packet(enum Type type);
		virtual ~Packet();
		
		void addHeader(const UPnP::PacketHeader& header);
		bool copy(Samurai::IO::Net::DatagramPacket& dgram);

	protected:
		enum Type m_type;
		std::vector<UPnP::PacketHeader> m_headers;
};

} // namespace UPnP

#endif // HAVE_SAMURAI_UPNP_COMMON_H