/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <string.h>
#include <samurai/samurai.h>
#include <samurai/io/net/interface.h>
#include <samurai/io/net/hardwareaddress.h>
#include <samurai/io/net/inetaddress.h>
#include <stdlib.h>
#include <string>

#ifdef SAMURAI_WINDOWS
#include <iphlpapi.h>
#define USE_ADAPTER_INFO
#endif

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
		bool getInfo(unsigned long info, int af = AF_INET);
		void extractHardwareAddress();
		void extractAddresses();
		void extractFlags();

	private:
		int m_mtu;
		interface_t m_ifnumber;
		char m_name[IFNAMSIZ];
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

		/* The base implementations report nothing, so an interface that has a
		 * name and an MTU has to hand them over itself. */
		const char* getName() const;
		interface_t getHandle() const;
		int getMtu() const;

	private:
		/* std::string rather than a strdup()ed char*, which nothing released.
		 * m_flags belongs to the base class and is not redeclared here. */
		std::string m_name;
		interface_t m_ifnumber;
		int m_mtu;
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
	memset(m_name, 0, sizeof(m_name));
	strncpy(m_name, name, sizeof(m_name) - 1);

	m_ifnumber = if_nametoindex(name);
	if (getInfo(SIOCGIFMTU))
		m_mtu = m_ifr.ifr_mtu;

	extractFlags();
	extractHardwareAddress();
	extractAddresses();
}

bool Samurai::IO::Net::NetworkInterfaceUnix::getInfo(unsigned long info, int af)
{
	int sock = socket(af, SOCK_DGRAM, 0);
	if (sock == -1) return false;

	memset(&m_ifr, 0, sizeof(m_ifr));
	strncpy(m_ifr.ifr_name, m_name, sizeof(m_ifr.ifr_name) - 1);
	m_ifr.ifr_addr.sa_family = af;

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
	struct ifaddrs* first = nullptr;
	struct sockaddr_dl* link;
	if (getifaddrs(&first) == 0)
	{
		for (struct ifaddrs* iface = first; iface; iface = iface->ifa_next)
		{
			if (!iface->ifa_addr)
				continue;

			if (iface->ifa_addr->sa_family == AF_LINK)
			{
				if (strcmp(iface->ifa_name, m_name) == 0)
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
		m_hwaddr = std::make_unique<Samurai::IO::Net::HardwareAddress>(hwaddr_bytes);
}

// FIXME: very Version::IPv4 centric!
void Samurai::IO::Net::NetworkInterfaceUnix::extractAddresses()
{
	if (getInfo(SIOCGIFADDR))
		m_address = std::make_unique<Samurai::IO::Net::InetAddress>(inet_ntoa(((struct sockaddr_in *)&m_ifr.ifr_addr)->sin_addr));

	if (getInfo(SIOCGIFNETMASK))
		m_netmask = std::make_unique<Samurai::IO::Net::InetAddress>(inet_ntoa(((struct sockaddr_in *)&m_ifr.ifr_addr)->sin_addr));

	if (any(m_flags & NetworkInterfaceFlags::Broadcast) && getInfo(SIOCGIFBRDADDR))
		m_broadcast = std::make_unique<Samurai::IO::Net::InetAddress>(inet_ntoa(((struct sockaddr_in *)&m_ifr.ifr_broadaddr)->sin_addr));

#ifdef SIOCGIFDSTADDR
	if (any(m_flags & NetworkInterfaceFlags::PointToPoint) && getInfo(SIOCGIFDSTADDR))
		m_destination = std::make_unique<Samurai::IO::Net::InetAddress>(inet_ntoa(((struct sockaddr_in *)&m_ifr.ifr_dstaddr)->sin_addr));
#endif
}

void Samurai::IO::Net::NetworkInterfaceUnix::extractFlags()
{
	if (getInfo(SIOCGIFFLAGS))
	{
		int f = m_ifr.ifr_flags;
		m_flags = NetworkInterfaceFlags::None;

		if (true
#ifdef IFF_UP
			&& (f & IFF_UP)
#endif
#ifdef IFF_RUNNING
			&& (f & IFF_RUNNING)
#endif
			)
			m_flags |= NetworkInterfaceFlags::Enabled;

		if (f & IFF_LOOPBACK)
			m_flags |= NetworkInterfaceFlags::Loopback;

		if (f & IFF_BROADCAST)
			m_flags |= NetworkInterfaceFlags::Broadcast;

		if (f & IFF_MULTICAST)
			m_flags |= NetworkInterfaceFlags::Multicast;

#ifdef IFF_POINTOPOINT
		if (f & IFF_POINTOPOINT)
			m_flags |= NetworkInterfaceFlags::PointToPoint;
#endif
	}
}

const char* Samurai::IO::Net::NetworkInterfaceUnix::getName() const
{
	return m_name;
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

const char* Samurai::IO::Net::NetworkInterfaceWindows::getName() const
{
	return m_name.c_str();
}

interface_t Samurai::IO::Net::NetworkInterfaceWindows::getHandle() const
{
	return m_ifnumber;
}

int Samurai::IO::Net::NetworkInterfaceWindows::getMtu() const
{
	return m_mtu;
}

#ifdef USE_ADAPTER_INFO
Samurai::IO::Net::NetworkInterfaceWindows::NetworkInterfaceWindows(PIP_ADAPTER_INFO info)
	: Samurai::IO::Net::NetworkInterface()
	, m_name(info->Description ? info->Description : "")
	, m_ifnumber((interface_t) info->Index)
	, m_mtu(0)
{
	if (info->AddressLength == 6)
	{
		uint8_t hwaddr_bytes[6];
		memcpy(hwaddr_bytes, info->Address, 6);
		if (hwaddr_bytes[0] || hwaddr_bytes[1] || hwaddr_bytes[2] || hwaddr_bytes[3] || hwaddr_bytes[4] || hwaddr_bytes[5])
			m_hwaddr = std::make_unique<Samurai::IO::Net::HardwareAddress>(hwaddr_bytes);
	}

	m_flags |= NetworkInterfaceFlags::Enabled;
	if (info->Type == MIB_IF_TYPE_LOOPBACK)   m_flags |= NetworkInterfaceFlags::Loopback;
	if (info->Type == MIB_IF_TYPE_PPP)        m_flags |= NetworkInterfaceFlags::PointToPoint;
	if (info->Type == MIB_IF_TYPE_ETHERNET)   m_flags |= (NetworkInterfaceFlags::Broadcast | NetworkInterfaceFlags::Multicast);

	/* Only the first address of the adapter is represented, so the list is not
	   walked; assigning inside a loop would leak all but the last anyway. */
	PIP_ADDR_STRING ipstr = &info->IpAddressList;
	if (ipstr)
	{
		m_address = std::make_unique<Samurai::IO::Net::InetAddress>(
			ipstr->IpAddress.String, Samurai::IO::Net::InetAddress::Version::IPv4);
		m_netmask = std::make_unique<Samurai::IO::Net::InetAddress>(
			ipstr->IpMask.String, Samurai::IO::Net::InetAddress::Version::IPv4);
	}
}


#else // USE_ADAPTER_INFO

Samurai::IO::Net::NetworkInterfaceWindows::NetworkInterfaceWindows(PIP_ADAPTER_ADDRESSES info)
	: Samurai::IO::Net::NetworkInterface()
	, m_name(info->AdapterName ? info->AdapterName : "")
	, m_ifnumber((interface_t) info->IfIndex)
	, m_mtu((int) info->Mtu)
{
	if (info->PhysicalAddressLength == 6)
	{
		uint8_t hwaddr_bytes[6];
		memcpy(hwaddr_bytes, info->PhysicalAddress, 6);
		if (hwaddr_bytes[0] || hwaddr_bytes[1] || hwaddr_bytes[2] || hwaddr_bytes[3] || hwaddr_bytes[4] || hwaddr_bytes[5])
			m_hwaddr = std::make_unique<Samurai::IO::Net::HardwareAddress>(hwaddr_bytes);
	}

	if (info->OperStatus == IfOperStatusUp)
		m_flags |= NetworkInterfaceFlags::Enabled;

	/* The negation has to apply to the masked bit, not to Flags: '!' binds
	   tighter than '&', so testing !Flags & BIT asks whether Flags is zero. */
	if (!(info->Flags & IP_ADAPTER_NO_MULTICAST))
		m_flags |= NetworkInterfaceFlags::Multicast;

	if (info->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
		m_flags |= NetworkInterfaceFlags::Loopback;

	if (info->IfType == IF_TYPE_PPP || info->IfType == IF_TYPE_TUNNEL)
		m_flags |= NetworkInterfaceFlags::PointToPoint;

	if (info->IfType == IF_TYPE_ETHERNET_CSMACD || info->IfType == IF_TYPE_IEEE80211)
	{
		m_flags |= NetworkInterfaceFlags::Broadcast;
	}

	/* The address lives on the adapter's first unicast entry; IP_ADAPTER_ADDRESSES
	   itself has no sockaddr. */
	PIP_ADAPTER_UNICAST_ADDRESS unicast = info->FirstUnicastAddress;
	struct sockaddr* sa = unicast ? unicast->Address.lpSockaddr : 0;

	if (sa && sa->sa_family == AF_INET)
	{
		struct sockaddr_in* addr4 = (struct sockaddr_in*) sa;
		m_address = std::make_unique<Samurai::IO::Net::InetAddress>();
		m_address->setRawAddress(&addr4->sin_addr, sizeof(addr4->sin_addr),
			Samurai::IO::Net::InetAddress::Version::IPv4);
	}
	else if (sa && sa->sa_family == AF_INET6)
	{
		struct sockaddr_in6* addr6 = (struct sockaddr_in6*) sa;
		m_address = std::make_unique<Samurai::IO::Net::InetAddress>();
		m_address->setRawAddress(&addr6->sin6_addr, sizeof(addr6->sin6_addr),
			Samurai::IO::Net::InetAddress::Version::IPv6);
	}
}
#endif // USE_ADAPTER_INFO
#endif // SAMURAI_WINDOWS


std::unique_ptr<Samurai::IO::Net::NetworkInterface> Samurai::IO::Net::NetworkInterface::getInterface(const Samurai::IO::Net::InetAddress& addr)
{
	std::vector<std::unique_ptr<NetworkInterface>> interfaces;
	if (!getInterfaces(interfaces)) return nullptr;

	for (std::unique_ptr<NetworkInterface>& iface : interfaces)
	{
		const InetAddress* candidate = iface->getAddress();
		if (candidate && *candidate == addr)
			return std::move(iface);
	}
	return nullptr;
}

std::unique_ptr<Samurai::IO::Net::NetworkInterface> Samurai::IO::Net::NetworkInterface::getInterface(const char* name)
{
	if (!name) return nullptr;

	std::vector<std::unique_ptr<NetworkInterface>> interfaces;
	if (!getInterfaces(interfaces)) return nullptr;

	for (std::unique_ptr<NetworkInterface>& iface : interfaces)
	{
		const char* candidate = iface->getName();
		if (candidate && strcmp(candidate, name) == 0)
			return std::move(iface);
	}
	return nullptr;
}

/*
 * The index rather than the address, so an interface with no IPv4 address can
 * still be named - which is what IPv6 multicast and a scope identifier need, and
 * what getInterfaces() cannot describe.
 */
interface_t Samurai::IO::Net::NetworkInterface::getIndexByName(const char* name)
{
	if (!name || !*name) return 0;

#ifdef SAMURAI_UNIX
	return (interface_t) if_nametoindex(name);
#endif

#ifdef SAMURAI_WINDOWS
	NET_LUID luid;
	if (ConvertInterfaceNameToLuidA(name, &luid) != NO_ERROR) return 0;

	NET_IFINDEX index = 0;
	if (ConvertInterfaceLuidToIndex(&luid, &index) != NO_ERROR) return 0;
	return (interface_t) index;
#endif
}


std::string Samurai::IO::Net::NetworkInterface::getNameByIndex(interface_t index)
{
	if (!index) return std::string();

#ifdef SAMURAI_UNIX
	char name[IF_NAMESIZE];
	memset(name, 0, sizeof(name));
	if (!if_indextoname((unsigned int) index, name)) return std::string();
	return std::string(name);
#endif

#ifdef SAMURAI_WINDOWS
	NET_LUID luid;
	if (ConvertInterfaceIndexToLuid((NET_IFINDEX) index, &luid) != NO_ERROR)
		return std::string();

	char name[256];
	memset(name, 0, sizeof(name));
	if (ConvertInterfaceLuidToNameA(&luid, name, sizeof(name)) != NO_ERROR)
		return std::string();
	return std::string(name);
#endif
}


bool Samurai::IO::Net::NetworkInterface::getInterfaces(std::vector<std::unique_ptr<NetworkInterface>>& interfaces)
{

#ifdef SAMURAI_UNIX
	struct if_nameindex* ifaces = if_nameindex();
	if (!ifaces) return false;
	for (size_t i = 0; ifaces[i].if_index; i++)
	{

		interfaces.push_back(std::make_unique<Samurai::IO::Net::NetworkInterfaceUnix>(ifaces[i].if_name));
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
			interfaces.push_back(std::make_unique<Samurai::IO::Net::NetworkInterfaceWindows>(adapter));
			adapter = adapter->Next;
		}
	}
	free(adapterInfo);
	return true;
#else // USE_ADAPTER_INFO

	DWORD flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_ALL_INTERFACES;
	PIP_ADAPTER_ADDRESSES adr = 0;
	PIP_ADAPTER_ADDRESSES ptr = 0;
	DWORD bufsize = sizeof(IP_ADAPTER_ADDRESSES);
	adr = (PIP_ADAPTER_ADDRESSES) malloc(bufsize);
	if (!adr) return false;

	ret = GetAdaptersAddresses(AF_UNSPEC, flags, 0, adr, &bufsize);
	if (ret == ERROR_BUFFER_OVERFLOW)
	{
		/* bufsize now holds the length the call wants, which is what the
		   second allocation has to ask for. */
		free(adr);
		adr = (PIP_ADAPTER_ADDRESSES) malloc(bufsize);
		if (!adr) return false;
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
			interfaces.push_back(std::make_unique<Samurai::IO::Net::NetworkInterfaceWindows>(ptr));
			ptr = ptr->Next;
		}
		free(adr);
		return true;
	}

	free(adr);
	return false;

#endif // USE_ADAPTER_INFO
#endif // SAMURAI_WINDOWS
}

Samurai::IO::Net::NetworkInterface::NetworkInterface()
	: m_hwaddr(nullptr)
	, m_address(nullptr)
	, m_netmask(nullptr)
	, m_broadcast(nullptr)
	, m_destination(nullptr)
	, m_flags(NetworkInterfaceFlags::None)
{

}

Samurai::IO::Net::NetworkInterface::~NetworkInterface() = default;

const char* Samurai::IO::Net::NetworkInterface::getName() const
{
	assert(! "Samurai::IO::Net::NetworkInterface::getName()");
	return nullptr;
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
	return m_address.get();
}

Samurai::IO::Net::InetAddress* Samurai::IO::Net::NetworkInterface::getBroadcastAddress() const
{
	return m_broadcast.get();
}

Samurai::IO::Net::InetAddress* Samurai::IO::Net::NetworkInterface::getNetmask() const
{
	return m_netmask.get();
}

Samurai::IO::Net::InetAddress* Samurai::IO::Net::NetworkInterface::getDestinationAddress() const
{
	return m_destination.get();
}

Samurai::IO::Net::HardwareAddress* Samurai::IO::Net::NetworkInterface::getHWAddress() const
{
	return m_hwaddr.get();
}

bool Samurai::IO::Net::NetworkInterface::isEnabled() const
{
	return any(m_flags & NetworkInterfaceFlags::Enabled);
}

bool Samurai::IO::Net::NetworkInterface::isMulticast() const
{

	return any(m_flags & NetworkInterfaceFlags::Multicast);
}

bool Samurai::IO::Net::NetworkInterface::isBroadcast() const
{
	return any(m_flags & NetworkInterfaceFlags::Broadcast);
}

bool Samurai::IO::Net::NetworkInterface::isLoopback() const
{
	return any(m_flags & NetworkInterfaceFlags::Loopback);
}

bool Samurai::IO::Net::NetworkInterface::isPointToPoint() const
{
	return any(m_flags & NetworkInterfaceFlags::PointToPoint);
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
