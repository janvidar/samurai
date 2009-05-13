/*
 * Copyright (C) 2001-2009 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/interface.h>
#include <samurai/io/net/hardwareaddress.h>
#include <samurai/io/net/inetaddress.h>
#include <stdlib.h>

#ifdef SAMURAI_WINDOWS
#include <iphlpapi.h>
#define USE_ADAPTER_INFO
#endif

enum NetworkInterfaceFlags
{
	InterfaceEnabled      = 0x01,
	InterfaceLoopback     = 0x02,
	InterfacePointToPoint = 0x04,
	InterfaceBroadcast    = 0x10,
	InterfaceMulticast    = 0x20,
};

namespace Samurai {
namespace IO {
namespace Net {


#ifdef SAMURAI_UNIX
class NetworkInterfaceUnix : public NetworkInterface
{
	public:
		NetworkInterfaceUnix(const char* name);

		const char* getName() const;
		interface_t getHandle() const;
		virtual int getMtu() const;
		
	private:
		bool getInfo(int info, int af = AF_INET);
		void extractHardwareAddress();
		void extractAddresses();
		void extractFlags();

	private:
		int m_mtu;
		interface_t m_ifnumber;
		struct ifreq m_ifr;
};

#endif // SAMURAI_UNIX

#ifdef SAMURAI_WINDOWS
class NetworkInterfaceWindows : public NetworkInterface
{
	public:
#ifdef USE_ADAPTER_INFO
		NetworkInterfaceWindows(PIP_ADAPTER_INFO info);
#else
		NetworkInterfaceWindows(PIP_ADAPTER_ADDRESSES info);
#endif

	private:
		interface_t ifnumber;
		char* name;
		int flags;
		int mtu;
};

#endif // SAMURAI_WINDOWS

}
}
}

#ifdef SAMURAI_UNIX
Samurai::IO::Net::NetworkInterfaceUnix::NetworkInterfaceUnix(const char* name)
	: Samurai::IO::Net::NetworkInterface()
	, m_mtu(0)
	, m_ifnumber(0)
{
	memset(&m_ifr, 0, sizeof(m_ifr));
	m_ifr.ifr_addr.sa_family = AF_INET;
	strcpy(m_ifr.ifr_name, name); // FIXME: Buffer overflow potential

	m_ifnumber = if_nametoindex(name);
	if (getInfo(SIOCGIFMTU))
		m_mtu = m_ifr.ifr_mtu;

	extractFlags();
	extractHardwareAddress();
	extractAddresses();
}

bool Samurai::IO::Net::NetworkInterfaceUnix::getInfo(int info, int af)
{
	int sock = socket(af, SOCK_DGRAM, 0);
	if (sock == -1) return false;
	if (ioctl(sock, info, &m_ifr) < 0)
	{
		close(sock);
		return false;
	}
	close(sock);
	return true;
}

void Samurai::IO::Net::NetworkInterfaceUnix::extractHardwareAddress()
{
	/* Extract MAC-address */
	uint8_t hwaddr_bytes[6];
	memset(hwaddr_bytes, 0, sizeof(hwaddr_bytes));
	
#if defined(SIOCGIFHWADDR)
	if (getInfo(SIOCGIFHWADDR))
	memcpy(hwaddr_bytes, &m_ifr.ifr_hwaddr.sa_data, 6);
#elif defined(SAMURAI_BSD)
	struct ifaddrs* first = 0;
	struct sockaddr_dl* link;
	if (getifaddrs(&first) == 0)
	{
		for (struct ifaddrs* iface = first; iface; iface = iface->ifa_next)
		{
			if ((iface->ifa_addr->sa_family == AF_LINK))
			{
				if (strcmp(iface->ifa_name, name) == 0 && iface->ifa_addr)
				{
					link = (struct sockaddr_dl*) iface->ifa_addr;
					if (link->sdl_alen == 6)
					{
						memcpy(hwaddr_bytes, LLADDR(link), 6);
					}
					break;
				}
			}
		}
		if (first)
			freeifaddrs(first);
	}
#endif // SIOCGIFHWADDR

	if (hwaddr_bytes[0] || hwaddr_bytes[1] || hwaddr_bytes[2] || hwaddr_bytes[3] || hwaddr_bytes[4] || hwaddr_bytes[5])
		m_hwaddr = new Samurai::IO::Net::HardwareAddress(hwaddr_bytes);
}

// FIXME: very IPv4 centric!
void Samurai::IO::Net::NetworkInterfaceUnix::extractAddresses()
{
	if (getInfo(SIOCGIFADDR)) 
		m_address = new Samurai::IO::Net::InetAddress(inet_ntoa(((struct sockaddr_in *)&m_ifr.ifr_addr)->sin_addr));

	if (getInfo(SIOCGIFNETMASK))
		m_netmask = new Samurai::IO::Net::InetAddress(inet_ntoa(((struct sockaddr_in *)&m_ifr.ifr_addr)->sin_addr));

	if (getInfo(SIOCGIFBRDADDR))
		m_broadcast = new Samurai::IO::Net::InetAddress(inet_ntoa(((struct sockaddr_in *)&m_ifr.ifr_addr)->sin_addr));

	if (getInfo(SIOCGIFBRDADDR))
		m_destination = new Samurai::IO::Net::InetAddress(inet_ntoa(((struct sockaddr_in *)&m_ifr.ifr_addr)->sin_addr));
}

void Samurai::IO::Net::NetworkInterfaceUnix::extractFlags()
{
	if (getInfo(SIOCGIFFLAGS))
	{
		int f = m_ifr.ifr_flags;
		m_flags = 0;

		if (true
#ifdef IFF_UP
			&& (f & IFF_UP)
#endif
#ifdef IFF_RUNNING
			&& (f & IFF_RUNNING)
#endif
			)
			m_flags |= InterfaceEnabled;

		if (f & IFF_LOOPBACK)
			m_flags |= InterfaceLoopback;

		if (f & IFF_BROADCAST)
			m_flags |= InterfaceBroadcast;

		if (f & IFF_MULTICAST)
			m_flags |= InterfaceMulticast;

#ifdef IFF_POINTOPOINT
		if (f & IFF_POINTOPOINT)
			m_flags |= InterfacePointToPoint;
#endif
	}
}

const char* Samurai::IO::Net::NetworkInterfaceUnix::getName() const
{
	return m_ifr.ifr_name;
}

int Samurai::IO::Net::NetworkInterfaceUnix::getMtu() const
{
	return m_mtu;
}

interface_t Samurai::IO::Net::NetworkInterfaceUnix::getHandle() const
{
	return m_ifnumber;
}

#endif // SAMURAI_UNIX


#ifdef SAMURAI_WINDOWS

#ifdef USE_ADAPTER_INFO
Samurai::IO::Net::NetworkInterfaceWindows::NetworkInterfaceWindows(PIP_ADAPTER_INFO info)
	: Samurai::IO::Net::NetworkInterface()
{
	m_name = strdup(info->Description); // AdapterName
	m_ifnumber = info->Index;
	
	if (info->AddressLength == 6)
	{
		uint8_t hwaddr_bytes[6];
		memcpy(hwaddr_bytes, info->Address, 6);
		if (hwaddr_bytes[0] || hwaddr_bytes[1] || hwaddr_bytes[2] || hwaddr_bytes[3] || hwaddr_bytes[4] || hwaddr_bytes[5])
			hwaddr = new Samurai::IO::Net::HardwareAddress(hwaddr_bytes);
	}
	
	m_flags |= InterfaceEnabled;
	if (info->Type == MIB_IF_TYPE_LOOPBACK)   m_flags |= InterfaceLoopback;
	if (info->Type == MIB_IF_TYPE_PPP)        m_flags |= InterfacePointToPoint;
	if (info->Type == MIB_IF_TYPE_ETHERNET)   m_flags |= (InterfaceBroadcast | InterfaceMulticast);
	
	PIP_ADDR_STRING ipstr = &info->IpAddressList;
	while (ipstr)
	{
		char ip[16]   = {0, };
		char mask[16] = {0, };

		strcpy(ip, ipstr->IpAddress.String);
		strcpy(mask, ipstr->IpMask.String);

		printf("IP %s/%s (adapter: %d, %s)\n", ip, mask, ifnumber, name);
		address = new Samurai::IO::Net::InetAddress(ip, Samurai::IO::Net::InetAddress::IPv4);
		netmask = new Samurai::IO::Net::InetAddress(mask, Samurai::IO::Net::InetAddress::IPv4);
		break;
		ipstr = ipstr->Next;
	}
	printf("\n");
}


#else // USE_ADAPTER_INFO

Samurai::IO::Net::NetworkInterfaceWindows::NetworkInterfaceWindows(PIP_ADAPTER_ADDRESSES info)
	: Samurai::IO::Net::NetworkInterface()
	
{
	name = strdup(info->AdapterName); // AdapterName
	ifnumber = info->IfIndex;
	
	if (info->PhysicalAddressLength == 6)
	{
		uint8_t hwaddr_bytes[6];
		memcpy(hwaddr_bytes, info->PhysicalAddress, 6);
		if (hwaddr_bytes[0] || hwaddr_bytes[1] || hwaddr_bytes[2] || hwaddr_bytes[3] || hwaddr_bytes[4] || hwaddr_bytes[5])
			hwaddr = new Samurai::IO::Net::HardwareAddress(hwaddr_bytes);
	}
	
	mtu = info->Mtu;
	
	if (info->OperStatus == IfOperStatusUp)
		flags |= InterfaceEnabled;
	
	if (!info->Flags & IP_ADAPTER_NO_MULTICAST)
		flags |= InterfaceMulticast;
	
	if (info->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
		flags |= InterfaceLoopback;
	
	if (info->IfType == IF_TYPE_PPP || info->IfType == IF_TYPE_TUNNEL)
		flags |= InterfacePointToPoint;
	
	if (info->IfType == IF_TYPE_ETHERNET_CSMACD || info->IfType == IF_TYPE_IEE80211)
	{
		flags |= InterfaceBroadcast;
	}
	
	PIP_ADAPTER_UNICAST_ADDRESSES ip = info->FirstUnicastAddress;
	struct sockaddr* sa = (struct sockaddr*) info->Address->lpSockaddr;
	struct sockaddr_in* addr4  = (struct sockaddr_in*) sa;
	struct sockaddr_in6* addr6 = (struct sockaddr_in6*) sa;

	if (sa->sa_family == AF_INET || sa->sa_family == AF_INET6)
	{
		address = new Samurai::IO::Net::InetAddress();
		if (sa->sa_family == AF_INET)
		{
			address->setRawAddress(addr4.sin_addr, sizeof(addr4.sin_addr), Samurai::IO::Net::InetAddress::IPv4);
		}
		else
		{
			address->setRawAddress(addr6.sin6_addr, sizeof(addr6.sin6_addr), Samurai::IO::Net::InetAddress::IPv6);
		}
	}
	// netmask = new Samurai::IO::Net::InetAddress(info->IpAddressList.IpMask.String);
	// netmask
}
#endif // USE_ADAPTER_INFO
#endif // SAMURAI_WINDOWS


Samurai::IO::Net::NetworkInterface* Samurai::IO::Net::NetworkInterface::getInterface(const Samurai::IO::Net::InetAddress& addr)
{
	(void) addr;
	return 0;
}

Samurai::IO::Net::NetworkInterface* Samurai::IO::Net::NetworkInterface::getInterface(const char* name)
{
	(void) name;
	return 0;
}

bool Samurai::IO::Net::NetworkInterface::getInterfaces(std::vector<NetworkInterface*>& interfaces)
{
	(void) interfaces;
	
	Samurai::IO::Net::NetworkInterface* iface = 0;

#ifdef SAMURAI_UNIX
	struct if_nameindex* ifaces = if_nameindex();
	if (!ifaces) return false;
	for (size_t i = 0; ifaces[i].if_index; i++)
	{
		
		iface = new Samurai::IO::Net::NetworkInterfaceUnix(ifaces[i].if_name);
		interfaces.push_back(iface);
	}
	if_freenameindex(ifaces);
	return true;
#endif

#ifdef SAMURAI_WINDOWS
	DWORD ret = 0;
#ifdef USE_ADAPTER_INFO
	PIP_ADAPTER_INFO adapterInfo;
	PIP_ADAPTER_INFO adapter = 0;
	DWORD bufsize = sizeof(IP_ADAPTER_INFO);
	adapterInfo = (IP_ADAPTER_INFO*) malloc(bufsize);
	
	ret = GetAdaptersInfo(adapterInfo, &bufsize);
	if (ret == ERROR_BUFFER_OVERFLOW)
	{
		free(adapterInfo);
		adapterInfo = (IP_ADAPTER_INFO*) malloc(bufsize);
		ret = GetAdaptersInfo(adapterInfo, &bufsize);
	}
	
	if (ret == NO_ERROR)
	{
		adapter = adapterInfo;
		while (adapter)
		{
			iface = new Samurai::IO::Net::NetworkInterfaceWindows(adapter);
			interfaces.push_back(iface);
			adapter = adapter->Next;
		}
	}
	free(adapterInfo);
	return true;
#else // USE_ADAPTER_INFO

	DWORD flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_INCLUDE_PREFIX | GAA_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_ALL_INTERFACES;
	PIP_ADAPTER_ADDRESSES adr = 0;
	PIP_ADAPTER_ADDRESSES ptr = 0;
	DWORD bufsize = sizeof(IP_ADAPTER_ADDRESSES);
	adr = (PIP_ADAPTER_ADDRESSES) malloc(bufsize);
	
	ret = GetAdaptersAddresses(AF_UNSPEC, flags, 0, adr, &bufsize);
	if (ret == ERROR_BUFFER_OVERFLOW)
	{
		free(adr);
		adr = (PIP_ADAPTER_ADDRESSES) malloc(&bufsize);
		ret = GetAdaptersAddresses(AF_UNSPEC, flags, 0, adr, &bufsize);
	}
	
	if (ret == ERROR_NO_DATA)
	{
		free(adr);
		return true;
	}
	
	if (ret == ERROR_SUCCESS)
	{
		ptr = adr;
		while (ptr)
		{
			iface = new Samurai::IO::Net::NetworkInterfaceWindows(ptr);
			interfaces.push_back(iface);
			ptr = ptr->Next;
		}
		free(adr);
		return true:
	}
	
	free(adr);
	return false;
);
	
#endif // USE_ADAPTER_INFO
#endif // SAMURAI_WINDOWS
}

Samurai::IO::Net::NetworkInterface::NetworkInterface()
	: m_hwaddr(0)
	, m_address(0)
	, m_netmask(0)
	, m_broadcast(0)
	, m_destination(0)
	, m_flags(0)
{

}

Samurai::IO::Net::NetworkInterface::~NetworkInterface()
{
	delete m_hwaddr;
	delete m_address;
	delete m_netmask;
	delete m_broadcast;
	delete m_destination;
}

const char* Samurai::IO::Net::NetworkInterface::getName() const
{
	assert(! "Samurai::IO::Net::NetworkInterface::getName()");
	return 0;
}

int Samurai::IO::Net::NetworkInterface::getMtu() const
{
	assert(! "Samurai::IO::Net::NetworkInterface::getMtu()");
	return 0;
}

interface_t Samurai::IO::Net::NetworkInterface::getHandle() const
{
	assert(! "Samurai::IO::Net::NetworkInterface::getHandle()");
	return (interface_t) 0;
}

Samurai::IO::Net::InetAddress* Samurai::IO::Net::NetworkInterface::getAddress() const
{
	return m_address;
}

Samurai::IO::Net::InetAddress* Samurai::IO::Net::NetworkInterface::getBroadcastAddress() const
{
	return m_broadcast;
}

Samurai::IO::Net::InetAddress* Samurai::IO::Net::NetworkInterface::getNetmask() const
{
	return m_netmask;
}

Samurai::IO::Net::InetAddress* Samurai::IO::Net::NetworkInterface::getDestinationAddress() const
{
	return m_destination;
}

Samurai::IO::Net::HardwareAddress* Samurai::IO::Net::NetworkInterface::getHWAddress() const
{
	return m_hwaddr;
}

bool Samurai::IO::Net::NetworkInterface::isEnabled() const
{
	return m_flags & InterfaceEnabled;
}

bool Samurai::IO::Net::NetworkInterface::isMulticast() const
{

	return m_flags & InterfaceMulticast;
}

bool Samurai::IO::Net::NetworkInterface::isBroadcast() const
{
	return m_flags & InterfaceBroadcast;
}

bool Samurai::IO::Net::NetworkInterface::isLoopback() const
{
	return m_flags & InterfaceLoopback;
}

bool Samurai::IO::Net::NetworkInterface::isPointToPoint() const
{
	return m_flags & InterfacePointToPoint;
}

bool Samurai::IO::Net::NetworkInterface::operator==(const NetworkInterface& other)
{
	return other.getHandle() == getHandle();
}

bool Samurai::IO::Net::NetworkInterface::operator==(const NetworkInterface* other)
{
	if (other == this)
		return true;
	return other->getHandle() == getHandle();
}

bool Samurai::IO::Net::NetworkInterface::operator!=(const NetworkInterface& other)
{
	return other.getHandle() != getHandle();
}

bool Samurai::IO::Net::NetworkInterface::operator!=(const NetworkInterface* other)
{
	if (other == this)
		return false;
	return other->getHandle() != getHandle();
}
