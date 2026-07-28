/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/socketglue.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/inetaddress.h>

#include <stdlib.h>
#include <string>

/**
 * inet_ntop
 */
static const char* net_address_to_string(int af, const void* src, char* dst, socklen_t cnt)
{
#ifdef SAMURAI_WINSOCK
	int address_length;
	DWORD size = cnt;
	struct sockaddr_storage addr;
	struct sockaddr_in* sin = (struct sockaddr_in*) &addr;
	struct sockaddr_in6* sin6 = (struct sockaddr_in6*) &addr;

	memset (&addr, 0, sizeof(addr));
	switch (af) {
		case AF_INET:
			size = sizeof (struct sockaddr_in);
			sin->sin_family = af;
			memcpy (&sin->sin_addr, src, sizeof (struct in_addr));
			break;

		case AF_INET6:
			size = sizeof (struct sockaddr_in6);
			sin6->sin6_family = af;
			memcpy (&sin6->sin6_addr, src, sizeof (struct in6_addr));
			break;
		default:
			return NULL;
	}

	if (WSAAddressToString ((LPSOCKADDR) &addr, address_length, NULL, dst, &size) == 0)
		return dst;

	return NULL;
#else
	return inet_ntop(af, src, dst, cnt);
#endif
}

static int net_string_to_address(int af, const char* src, void* dst)
{
#ifdef SAMURAI_WINSOCK
	int ret, size;
	struct sockaddr_in  addr4;
	struct sockaddr_in6 addr6;
	struct sockaddr* addr = 0;
	if (af == AF_INET6)
	{
		// if (net_is_ipv6_supported() != 1) return -1;
		size = sizeof(struct sockaddr_in6);
		addr = (struct sockaddr*) &addr6;
	}
	else
	{
		size = sizeof(struct sockaddr_in);
		addr = (struct sockaddr*) &addr4;
	}

// 	if (!net_initialized)
// 		net_initialize();

	ret = WSAStringToAddressA((char*) src, af, NULL, addr, &size);
	if (ret == -1)
	{
		return -1;
	}

	if (af == AF_INET6)
	{
		memcpy(dst, &addr6.sin6_addr, sizeof(addr6.sin6_addr));
	}
	else
	{
		memcpy(dst, &addr4.sin_addr, sizeof(addr4.sin_addr));
	}

	return 1;
#else
	return inet_pton(af, src, dst);
#endif
}


Samurai::IO::Net::InetAddress::InetAddress() : version(Unspecified), data(0), resolver(0), resolveState(Unresolved), dnsevent(0)
{
	data = new Samurai::IO::Net::__InternalAddress();
	memset(data, 0, sizeof(struct Samurai::IO::Net::__InternalAddress));
}

Samurai::IO::Net::InetAddress::InetAddress(enum Version ip_version) : version(ip_version), data(0), resolver(0), resolveState(Unresolved), dnsevent(0)
{
	data = new Samurai::IO::Net::__InternalAddress();
	memset(data, 0, sizeof(struct Samurai::IO::Net::__InternalAddress));
}

Samurai::IO::Net::InetAddress::InetAddress(const std::string& address, enum Version ip_version) : version(Unspecified), data(0), resolver(0), resolveState(Unresolved), dnsevent(0)
{
	version = ip_version;
	data = new Samurai::IO::Net::__InternalAddress();
	memset(data, 0, sizeof(struct Samurai::IO::Net::__InternalAddress));
	
	// printf("InetAddress::InetAddress(): %s\n", address.c_str());
	
	bool ok = false;
	
	hostname = address;
	
	if (!address.size()) {
		/* INADDR_ANY */
		ok = true;
		version = IPv4;
		
	}
	else
	{
		if (version == Unspecified)
		{
			/* If address is indeed an IP address (as opposed to a hostname),
			   we will try to autodetect it and the address family. */
			ok = stringToAddress(IPv4, address.c_str(), data);
			if (ok)
			{
				// printf("Unspec is OK - IPv4\n");
				version = IPv4;
			}
			else
			{
				ok = stringToAddress(IPv6, address.c_str(), data);
				if (ok)
				{
					// printf("Unspec is OK - IPv6\n");
					version = IPv6;
				}
			}
		}
		else
		{
			if (version == IPv4)
			{
				// printf("Specified IPv4\n");
				ok = stringToAddress(IPv4, address.c_str(), data);
			}
			else if (version == IPv6)
			{
				// printf("Specified IPv6\n");
				ok = stringToAddress(IPv6, address.c_str(), data);
			}
		}
	}
	

	if (!ok)
	{
		// printf("NOT OK!\n");
		// error in string, or this is not an IP address.
		// let's try to resolve it
		memset(data, 0, sizeof(struct Samurai::IO::Net::__InternalAddress));
		version = Unspecified;
		// FIXME: Maybe this is indeed a name? Perhaps we should look up a IPv6 name, when IPv6 is specified?
	}
	else
	{
		// printf("OK!\n\n\n");
		resolveState = Resolved;
	}
	
}


Samurai::IO::Net::InetAddress::InetAddress(const Samurai::IO::Net::InetAddress& address) : ResolveEventHandler(), version(Unspecified), data(0), resolver(0), resolveState(Unresolved), dnsevent(0)
{
	version = address.version;
	data = new Samurai::IO::Net::__InternalAddress();
	memcpy(data, address.data, sizeof(struct Samurai::IO::Net::__InternalAddress));
	hostname = address.hostname;
	resolveState = address.resolveState;
}


Samurai::IO::Net::InetAddress::InetAddress(const Samurai::IO::Net::InetAddress* address) : version(Unspecified), data(0), resolver(0), resolveState(Unresolved), dnsevent(0)
{
	version = address->version;
	data = new Samurai::IO::Net::__InternalAddress();
	memcpy(data, address->data, sizeof(struct Samurai::IO::Net::__InternalAddress));
	hostname = address->hostname;
	resolveState = address->resolveState;
}


Samurai::IO::Net::InetAddress::~InetAddress()
{
	/* The resolver holds this object as its event handler. Clearing dnsevent
	   before destroying it means a callback arriving during teardown cannot be
	   forwarded to a user handler that believes this address is still alive. */
	dnsevent = 0;

	delete resolver;
	resolver = 0;

	delete data;
	data = 0;
}


bool Samurai::IO::Net::InetAddress::setRawAddress(void* data_, size_t length, enum Samurai::IO::Net::InetAddress::Version ip_version)
{
	if (ip_version == Samurai::IO::Net::InetAddress::IPv4 && length < sizeof(struct in_addr)) return false;
	if (ip_version == Samurai::IO::Net::InetAddress::IPv6 && length < sizeof(struct in6_addr)) return false;
	
 	version = ip_version;
	memset(data, 0, sizeof(struct Samurai::IO::Net::__InternalAddress));
	memcpy(data, data_, length);
	resolveState = Resolved;
	return true;
}


uint32_t Samurai::IO::Net::InetAddress::getIPv4HostOrder() const
{
	if (version != IPv4 || !data) return 0;
	return ntohl(X_IP4_32);
}


bool Samurai::IO::Net::InetAddress::isValid()
{
	if (version == IPv4) {
#ifdef SAMURAI_POSIX
		const uint32_t host_order = getIPv4HostOrder();
		return host_order && !IN_BADCLASS(host_order) && !IN_EXPERIMENTAL(host_order);
#else
		return true; // FIXME
#endif
	} else if (version == IPv6) {
#ifdef SAMURAI_POSIX
		return (X_IP6_32[0] || X_IP6_32[1] || X_IP6_32[2] || X_IP6_32[3]);
#else
		return true; // FIXME
#endif
	} else {
		return false;
	}
}


bool Samurai::IO::Net::InetAddress::isMulticast()
{
	if (version == IPv4) {
		const uint32_t host_order = getIPv4HostOrder();
		return (host_order >= 0xe0000000 && host_order < 0xf0000000);
	} else if (version == IPv6) {
#ifdef SAMURAI_POSIX
		return (X_IP6_08[0] == 0xff);
#else
		return false; // FIXME
#endif
	} else {
		return false;
	}
}


bool Samurai::IO::Net::InetAddress::isPrivate()
{
	if (version == IPv4) {
		if (ntohl(X_IP4_32) >= 0x0a000000 && ntohl(X_IP4_32) < 0x0b000000) return true; /* 10.0.0.0/8 */
		if (ntohl(X_IP4_32) >= 0xac100000 && ntohl(X_IP4_32) < 0xac150000) return true; /* 172.16.0.0/20 */
		if (ntohl(X_IP4_32) >= 0xc0a80000 && ntohl(X_IP4_32) < 0xc0a90000) return true; /* 192.168.0.0/16 */
		return false;
	} else if (version == IPv6) {
		// TODO: Implement this!
		return false;
	} else {
		return false;
	}
}


bool Samurai::IO::Net::InetAddress::isLoopback() const
{
	if (version == IPv4) {
		return ((getIPv4HostOrder() & 0xff000000) == 0x7f000000);
	} else if (version == IPv6) {
#ifdef SAMURAI_POSIX
		return (X_IP6_32[0] == 0 && X_IP6_32[1] == 0 && X_IP6_32[2] == 0 && X_IP6_16[6] == 0 && X_IP6_08[14] == 0 && X_IP6_08[15] == 1);
#else
		return false; // FIXME
#endif
	} else {
		return false;
	}
}


/* NOTE: This used to cache the formatted address in a mutable char* member
   and hand out a pointer to it, so the result went stale when the address
   changed and dangled once the object died. Formatted on demand instead. */
std::string Samurai::IO::Net::InetAddress::getAddress() const
{
	if (resolveState != Resolved)
		return hostname;

	char buf[INET6_ADDRSTRLEN+1] = { 0, };
	const char* ret = 0;

	if (version == IPv4) {
		ret = net_address_to_string(AF_INET, (void*) &data->internal.in, buf, INET_ADDRSTRLEN);
	} else if (version == IPv6) {
		ret = net_address_to_string(AF_INET6, (void*) &data->internal.in6, buf, INET6_ADDRSTRLEN);
	}

	if (!ret)
		return std::string();

	return std::string(buf);
}


std::string Samurai::IO::Net::InetAddress::toString() const
{
	const std::string string = getAddress();
/*
	QDBG("toString: '%s'", string);
*/
	return string;
}


enum Samurai::IO::Net::InetAddress::Version Samurai::IO::Net::InetAddress::getType() const {
	return version;
}


bool Samurai::IO::Net::InetAddress::operator==(const Samurai::IO::Net::InetAddress& copy)
{
	if (&copy == this) return true;
	if (copy.version != version) return false;
	
	if (!memcmp(&data->internal, &copy.data->internal, sizeof(struct __InternalAddress)))
		return true;
	
	return false;
}


bool Samurai::IO::Net::InetAddress::operator!=(const Samurai::IO::Net::InetAddress& copy)
{
	if (&copy == this) return false;
	if (copy.version != version) return true;
	
	if (!memcmp(&data->internal, &copy.data->internal, sizeof(struct __InternalAddress)))
		return false;
	return true;
}

Samurai::IO::Net::InetAddress& Samurai::IO::Net::InetAddress::operator=(const std::string& address)
{
	delete data; data = 0;
	delete resolver; resolver = 0;
	
	version = Unspecified;
	data = new Samurai::IO::Net::__InternalAddress();
	memset(data, 0, sizeof(struct Samurai::IO::Net::__InternalAddress));
	
	if (address == "") return *this;
	int ret;

	
	ret = net_string_to_address(AF_INET, address.c_str(), (void*) &data->internal.in);
	if (ret > 0) {
		version = IPv4;
	} else {
		ret = net_string_to_address(AF_INET6, address.c_str(), (void*) &data->internal.in6);
		if (ret > 0) {
			version = IPv6;
		}
		else
		{
			// Check for [..::x]-encoded addresses
			if (address.size() > 2 && address[0] == '[' && address[address.size()-1] == ']')
			{
				std::string addr2 = address.substr(1, address.size()-2);
				ret = net_string_to_address(AF_INET6, addr2.c_str(), (void*) &data->internal.in6);
				if (ret > 0) {
					version = IPv6;
				}
			}
		}
		
	}
	
	if (ret <= 0) {
		// error in string, or this is not an IP address.
		// let's try to resolve it
		hostname = address;
		memset(data, 0, sizeof(struct Samurai::IO::Net::__InternalAddress));
		version = Unspecified;
	} else {
		resolveState = Resolved;
	}
	
	return *this;
}

Samurai::IO::Net::InetAddress& Samurai::IO::Net::InetAddress::operator=(const Samurai::IO::Net::InetAddress& copy)
{
	delete data; data = 0;
	delete resolver; resolver = 0;


	version = copy.version;
	data = new Samurai::IO::Net::__InternalAddress();
	memcpy(data, copy.data, sizeof(struct Samurai::IO::Net::__InternalAddress));
	hostname = copy.hostname;
	resolveState = copy.resolveState;
	return *this;
}



bool Samurai::IO::Net::InetAddress::isResolved()
{
	if (isValid() && resolveState == Resolved) return true;
	return false;
}


void Samurai::IO::Net::InetAddress::EventHostFound(Samurai::IO::Net::InetAddress* address)
{
	if (!address || !address->data) return;

	/* NOTE: this used to allocate a fresh __InternalAddress over the one
	   already held, leaking it on every resolution. Every constructor
	   allocates it, so there is always a block to copy into. */
	version = address->version;
	memcpy(data, address->data, sizeof(struct Samurai::IO::Net::__InternalAddress));
	resolveState = Resolved;

	/* Cleared before the callback, not after: the handler is free to destroy
	   this object, and coming back to touch a member afterwards would be a
	   use-after-free. */
	Samurai::IO::Net::ResolveEventHandler* handler = dnsevent;
	dnsevent = 0;

	if (handler) handler->EventHostFound(this);
}


void Samurai::IO::Net::InetAddress::EventHostError(enum Samurai::IO::Net::DNS::Resolver::Error error)
{
	resolveState = ResolveError;
	if (dnsevent) {
		dnsevent->EventHostError(error);
	}
	dnsevent = 0;
}


void Samurai::IO::Net::InetAddress::lookup(ResolveEventHandler* eventHandler)
{
	if (eventHandler) dnsevent = eventHandler;

	if (resolveState == Resolved)
	{
		Samurai::IO::Net::ResolveEventHandler* handler = dnsevent;
		dnsevent = 0;
		if (handler) handler->EventHostFound(this);
		return;
	}

	/* NOTE: this used to assign straight over 'resolver', leaking the
	   previous one on every repeated lookup. */
	delete resolver;
	resolver = 0;

	resolver = Samurai::IO::Net::DNS::Resolver::getHostByName(this, hostname.c_str()); /* FIXME: std::string-ify */
}


/*
 * NOTE: This replaces a hand-written dotted-quad and IPv6 parser
 * (stringToAddress) of roughly 150 lines. It mis-parsed "::" compression,
 * never accepted an IPv4-mapped "::ffff:1.2.3.4" at all, and ended in a
 * free() applied to the caller's buffer - which callers were passing
 * std::string::c_str() into. inet_pton() is in POSIX and Winsock and does
 * not have those problems.
 */
bool Samurai::IO::Net::InetAddress::stringToAddress(enum Samurai::IO::Net::InetAddress::Version version, const char* address, struct __InternalAddress* data)
{
	if (!address || !*address || !data) return false;

	std::string text(address);

	/* Accept the bracketed form used in URLs and by toString(). */
	if (text.size() > 2 && text.front() == '[' && text.back() == ']')
		text = text.substr(1, text.size() - 2);

	if (version == Samurai::IO::Net::InetAddress::IPv4)
		return net_string_to_address(AF_INET, text.c_str(), (void*) &data->internal.in) > 0;

	if (version == Samurai::IO::Net::InetAddress::IPv6)
		return net_string_to_address(AF_INET6, text.c_str(), (void*) &data->internal.in6) > 0;

	return false;
}
