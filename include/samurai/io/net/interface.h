/*
 * Copyright (C) 2001-2009 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_NETWORK_INTERFACE_H
#define HAVE_SAMURAI_NETWORK_INTERFACE_H

#include <vector>
#include <samurai/io/net/socketglue.h>

namespace Samurai {
namespace IO {
namespace Net {

class InetAddress;
class HardwareAddress;
class NetworkInterfacePrivate;

class NetworkInterfaceIterator
{
	
};


/**
 * FIXME:
 * 1) Does not provide means to extract multiple IP addresses for one interface.
 * 2) No IPv6 support (see 1).
 */
class NetworkInterface
{
	public:
		static NetworkInterface* getInterface(const InetAddress& addr);
		static NetworkInterface* getInterface(const char* name);
		static bool getInterfaces(std::vector<NetworkInterface*>& interfaces);
	
	public:
		virtual ~NetworkInterface();

		virtual InetAddress* getAddress() const;
		virtual InetAddress* getBroadcastAddress() const;
		virtual InetAddress* getNetmask() const;
		virtual InetAddress* getDestinationAddress() const;

		/**
		 * Returns the MAC address.
		 * This is a string
		 */
		virtual HardwareAddress* getHWAddress() const;

		/**
		 * Returns the name of the networking interface.
		 */
		virtual const char* getName() const;
		
		/**
		 * Returns a handle for the networking interface.
		 * This is useful for multicast where the networking
		 * interface must be bound to.
		 */
		virtual interface_t getHandle() const;
		
		/**
		 * Retreive the Maximum Transmission Unit for the interface.
		 */
		virtual int getMtu() const;
		
		/**
		 * Is the interface "up" and "running"?
		 */
		virtual bool isEnabled() const;
		
		/**
		 * Is the interface multicast enabled?
		 */
		virtual bool isMulticast() const;
		
		/**
		 * Is the interface broadcast enabled?
		 */
		virtual bool isBroadcast() const;
		
		/**
		 * Is the interface a loopback interface?
		 */
		virtual bool isLoopback() const;
		
		/**
		 * Is the interface a point to point link?
		 * If it is, use getDestinationAddress() to obtain remote peer.
		 */
		virtual bool isPointToPoint() const;

		virtual bool operator==(const NetworkInterface& other);
		virtual bool operator==(const NetworkInterface* other);
		virtual bool operator!=(const NetworkInterface& other);
		virtual bool operator!=(const NetworkInterface* other);
		
	protected:
		NetworkInterface();
		
	protected:
		HardwareAddress* m_hwaddr;
		InetAddress* m_address;
		InetAddress* m_netmask;
		InetAddress* m_broadcast;
		InetAddress* m_destination;
		int m_flags;
};

}
}
}

#endif // HAVE_SAMURAI_NETWORK_INTERFACE_H

