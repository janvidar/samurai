/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
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

namespace Samurai {
namespace IO {
namespace Net {

#ifdef SAMURAI_UNIX
class NetworkInterfaceUnix : public NetworkInterface
{
	public:
		NetworkInterfaceUnix(const char* name);
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
};
#endif // SAMURAI_WINDOWS

}
}
}

#ifdef SAMURAI_UNIX

static bool unix_get_interface_info(struct ifreq& ifr, int info)
{
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == -1) return false;
	if (ioctl(sock, info, &ifr) < 0)
	{
		close(sock);
		return false;
	}
	close(sock);
	return true;
}

Samurai::IO::Net::NetworkInterfaceUnix::NetworkInterfaceUnix(const char* name_) : Samurai::IO::Net::NetworkInterface()
{
	name        = strdup(name_);
	ifnumber    = if_nametoindex(name);
	
	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_addr.sa_family = AF_INET;
	strcpy(ifr.ifr_name, name);
	
	if (unix_get_interface_info(ifr, SIOCGIFADDR))
		address = new Samurai::IO::Net::InetAddress(inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));

	if (unix_get_interface_info(ifr, SIOCGIFNETMASK))
		netmask = new Samurai::IO::Net::InetAddress(inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
	
	if (unix_get_interface_info(ifr, SIOCGIFBRDADDR))
		broadcast   = new Samurai::IO::Net::InetAddress(inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));

	if (unix_get_interface_info(ifr, SIOCGIFBRDADDR))
		destination = new Samurai::IO::Net::InetAddress(inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
	
	if (unix_get_interface_info(ifr, SIOCGIFMTU))
		mtu = ifr.ifr_mtu;
	
	
	if (unix_get_interface_info(ifr, SIOCGIFFLAGS))
	{
		int my_flags = ifr.ifr_flags;
		
		if (
#ifdef IFF_UP
		my_flags & IFF_UP &&
#endif
#ifdef IFF_RUNNING
		my_flags & IFF_RUNNING &&
#endif
			true)
		flags |= InterfaceEnabled;
		
		if (my_flags & IFF_LOOPBACK)
			flags |= InterfaceLoopback;
		
#ifdef IFF_POINTOPOINT
		if (my_flags & IFF_POINTOPOINT)
			flags |= InterfacePointToPoint;
#endif

		if (my_flags & IFF_BROADCAST)
			flags |= InterfaceBroadcast;

		if (my_flags & IFF_MULTICAST)
			flags |= InterfaceMulticast;
	}

	
	
	uint8_t hwaddr_bytes[6];
	memset(hwaddr_bytes, 0, sizeof(hwaddr_bytes));
	
#ifdef SIOCGIFHWADDR
	if (unix_get_interface_info(ifr, SIOCGIFHWADDR))
	memcpy(hwaddr_bytes, &ifr.ifr_hwaddr.sa_data, 6);
	
	// Must be ARPHRD_ETHER (1)
	// if (ifr.ifr_hwaddr.sa_family != 1)
	//	memset(hwaddr_bytes, 0, sizeof(hwaddr_bytes));

#else // !SIOCGIFHWADDR
#ifdef SAMURAI_BSD
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
#endif // SAMURAI_BSD
#endif // SIOCGIFHWADDR

	if (hwaddr_bytes[0] || hwaddr_bytes[1] || hwaddr_bytes[2] || hwaddr_bytes[3] || hwaddr_bytes[4] || hwaddr_bytes[5])
		hwaddr = new Samurai::IO::Net::HardwareAddress(hwaddr_bytes);
}
#endif // SAMURAI_UNIX


#ifdef SAMURAI_WINDOWS

#ifdef USE_ADAPTER_INFO
Samurai::IO::Net::NetworkInterfaceWindows::NetworkInterfaceWindows(PIP_ADAPTER_INFO info) : Samurai::IO::Net::NetworkInterface()
{
	name = strdup(info->Description); // AdapterName
	ifnumber = info->Index;
	
	if (info->AddressLength == 6)
	{
		uint8_t hwaddr_bytes[6];
		memcpy(hwaddr_bytes, info->Address, 6);
		if (hwaddr_bytes[0] || hwaddr_bytes[1] || hwaddr_bytes[2] || hwaddr_bytes[3] || hwaddr_bytes[4] || hwaddr_bytes[5])
			hwaddr = new Samurai::IO::Net::HardwareAddress(hwaddr_bytes);
	}
	
	flags |= InterfaceEnabled;
	
	if (info->Type == MIB_IF_TYPE_LOOPBACK)
		flags |= InterfaceLoopback;
	
	if (info->Type == MIB_IF_TYPE_PPP)
		flags |= InterfacePointToPoint;
	
	if (info->Type == MIB_IF_TYPE_ETHERNET)
	{
		flags |= InterfaceBroadcast;
		flags |= InterfaceMulticast;
	}
	
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

Samurai::IO::Net::NetworkInterfaceWindows::NetworkInterfaceWindows(PIP_ADAPTER_ADDRESSES info) : Samurai::IO::Net::NetworkInterface()
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

Samurai::IO::Net::NetworkInterface::NetworkInterface() :
	hwaddr(0),
	ifnumber(0),
	name(0),
	flags(0),
	mtu(0),
	address(0),
	netmask(0),
	broadcast(0),
	destination(0)
{

}


Samurai::IO::Net::NetworkInterface::~NetworkInterface()
{
	free(name);
	delete hwaddr;
	delete address;
	delete netmask;
	delete broadcast;
	delete destination;
}

const char* Samurai::IO::Net::NetworkInterface::getName() const
{
	return name;
}

int Samurai::IO::Net::NetworkInterface::getMtu() const
{
	return mtu;
}

interface_t Samurai::IO::Net::NetworkInterface::getHandle() const
{
	return ifnumber;
}

Samurai::IO::Net::InetAddress* Samurai::IO::Net::NetworkInterface::getAddress() const
{
	return address;
}

Samurai::IO::Net::InetAddress* Samurai::IO::Net::NetworkInterface::getBroadcastAddress() const
{
	return broadcast;
}

Samurai::IO::Net::InetAddress* Samurai::IO::Net::NetworkInterface::getNetmask() const
{
	return netmask;
}

Samurai::IO::Net::InetAddress* Samurai::IO::Net::NetworkInterface::getDestinationAddress() const
{
	return destination;
}

Samurai::IO::Net::HardwareAddress* Samurai::IO::Net::NetworkInterface::getHWAddress() const
{
	return hwaddr;
}

bool Samurai::IO::Net::NetworkInterface::isEnabled() const
{

	return flags & InterfaceEnabled;
}

bool Samurai::IO::Net::NetworkInterface::isMulticast() const
{

	return flags & InterfaceMulticast;
}

bool Samurai::IO::Net::NetworkInterface::isBroadcast() const
{
	return flags & InterfaceBroadcast;
}

bool Samurai::IO::Net::NetworkInterface::isLoopback() const
{
	return flags & InterfaceLoopback;
}

bool Samurai::IO::Net::NetworkInterface::isPointToPoint() const
{
	return flags & InterfacePointToPoint;
}


