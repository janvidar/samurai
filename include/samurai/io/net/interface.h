/*
 * Copyright (C) 2001-2009 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_NETWORK_INTERFACE_H
#define HAVE_SAMURAI_NETWORK_INTERFACE_H

#include <vector>
#include <samurai/io/net/socketglue.h>
#include <samurai/bitmask.h>
#include <memory>
#include <vector>

namespace Samurai {
namespace IO {
namespace Net {

class InetAddress;
class HardwareAddress;
class NetworkInterfacePrivate;

/**
 * FIXME:
 * 1) Does not provide means to extract multiple IP addresses for one interface.
 * 2) No Version::IPv6 support (see 1).
 */
/** Capabilities and state of an interface, as reported by the platform. */
enum class NetworkInterfaceFlags : unsigned
{
	None            = 0x00,
	Enabled         = 0x01,
	Loopback        = 0x02,
	PointToPoint    = 0x04,
	Broadcast       = 0x10,
	Multicast       = 0x20,
};

class NetworkInterface
{
	public:
		/**
		 * Find the interface holding 'addr', or null if no local interface
		 * has that address.
		 */
		static std::unique_ptr<NetworkInterface> getInterface(const InetAddress& addr);

		/**
		 * Find the interface called 'name', or null if there is none. The name
		 * is the one getName() reports.
		 */
		static std::unique_ptr<NetworkInterface> getInterface(const char* name);
		/**
		 * Enumerate the local interfaces.
		 *
		 * The caller owns what is appended; the vector holds unique_ptr because
		 * the raw-pointer form said nothing about who released them, and both
		 * in-tree callers leaked every entry.
		 */
		static bool getInterfaces(std::vector<std::unique_ptr<NetworkInterface>>& interfaces);
	
	public:
		virtual ~NetworkInterface();

		/* Releases raw pointers in its destructor, so the implicit copy
		 * operations would release them a second time. */
		NetworkInterface(const NetworkInterface&) = delete;
		NetworkInterface& operator=(const NetworkInterface&) = delete;

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
		std::unique_ptr<HardwareAddress> m_hwaddr;
		std::unique_ptr<InetAddress> m_address;
		std::unique_ptr<InetAddress> m_netmask;
		std::unique_ptr<InetAddress> m_broadcast;
		std::unique_ptr<InetAddress> m_destination;
		NetworkInterfaceFlags m_flags;
};

/* NetworkInterfaceFlags is a flag set; see samurai/bitmask.h. */
SAMURAI_DECLARE_BITMASK(NetworkInterfaceFlags)

}
}
}

#endif // HAVE_SAMURAI_NETWORK_INTERFACE_H

