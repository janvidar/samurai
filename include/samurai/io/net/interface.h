/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
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

class NetworkInterface
{
	public:
		enum Flags
		{
			InterfaceEnabled      = 0x01,
			InterfaceLoopback     = 0x02,
			InterfacePointToPoint = 0x04,
			InterfaceBroadcast    = 0x10,
			InterfaceMulticast    = 0x20,
		};

	public:
		static NetworkInterface* getInterface(const InetAddress& addr);
		static NetworkInterface* getInterface(const char* name);
		static bool getInterfaces(std::vector<NetworkInterface*>& interfaces);
	
	public:
		virtual ~NetworkInterface();

		InetAddress* getAddress() const;
		InetAddress* getBroadcastAddress() const;
		InetAddress* getNetmask() const;
		InetAddress* getDestinationAddress() const;

		/*
		 * Returns the MAC address.
		 * This is a string
		 */
		HardwareAddress* getHWAddress() const;

		/**
		 * Returns the name of the networking interface.
		 */
		const char* getName() const;
		
		/**
		 * Returns a handle for the networking interface.
		 * This is useful for multicast where the networking
		 * interface must be bound to.
		 */
		interface_t getHandle() const;
		
		/**
		 * Retreive the Maximum Transmission Unit for the interface.
		 */
		int getMtu() const;
		
		/**
		 * Is the interface "up" and "running"?
		 */
		bool isEnabled() const;
		
		/**
		 * Is the interface multicast enabled?
		 */
		bool isMulticast() const;
		
		/**
		 * Is the interface broadcast enabled?
		 */
		bool isBroadcast() const;
		
		/**
		 * Is the interface a loopback interface?
		 */
		bool isLoopback() const;
		
		/**
		 * Is the interface a point to point link?
		 * If it is, use getDestinationAddress() to obtain remote peer.
		 */
		bool isPointToPoint() const;
		
	protected:
		NetworkInterface();

	protected:
		HardwareAddress* hwaddr;
		
		interface_t ifnumber;
		char* name;
		int flags;
		int mtu;
		
		InetAddress* address;
		InetAddress* netmask;
		InetAddress* broadcast;
		InetAddress* destination;
};

}
}
}

#endif // HAVE_SAMURAI_NETWORK_INTERFACE_H

