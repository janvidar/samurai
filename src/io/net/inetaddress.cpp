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
#include <memory>

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
			return nullptr;
	}

	if (WSAAddressToString ((LPSOCKADDR) &addr, address_length, nullptr, dst, &size) == 0)
		return dst;

	return nullptr;
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

	ret = WSAStringToAddressA((char*) src, af, nullptr, addr, &size);
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


Samurai::IO::Net::InetAddress::InetAddress() : version(Unspecified), data(nullptr), resolver(nullptr), resolveState(ResolveState::Unresolved), dnsevent(nullptr)
{
	data = std::make_unique<Samurai::IO::Net::__InternalAddress>();
	memset(data.get(), 0, sizeof(struct Samurai::IO::Net::__InternalAddress));
}

Samurai::IO::Net::InetAddress::InetAddress(enum Version ip_version) : version(ip_version), data(nullptr), resolver(nullptr), resolveState(ResolveState::Unresolved), dnsevent(nullptr)
{
	data = std::make_unique<Samurai::IO::Net::__InternalAddress>();
	memset(data.get(), 0, sizeof(struct Samurai::IO::Net::__InternalAddress));
}

Samurai::IO::Net::InetAddress::InetAddress(const std::string& address, enum Version ip_version) : version(Unspecified), data(nullptr), resolver(nullptr), resolveState(ResolveState::Unresolved), dnsevent(nullptr)
{
	version = ip_version;
	data = std::make_unique<Samurai::IO::Net::__InternalAddress>();
	memset(data.get(), 0, sizeof(struct Samurai::IO::Net::__InternalAddress));
	
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
			ok = stringToAddress(IPv4, address.c_str(), data.get());
			if (ok)
			{
				// printf("Unspec is OK - IPv4\n");
				version = IPv4;
			}
			else
			{
				ok = stringToAddress(IPv6, address.c_str(), data.get());
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
				ok = stringToAddress(IPv4, address.c_str(), data.get());
			}
			else if (version == IPv6)
			{
				// printf("Specified IPv6\n");
				ok = stringToAddress(IPv6, address.c_str(), data.get());
			}
		}
	}
	

	if (!ok)
	{
		// printf("NOT OK!\n");
		// error in string, or this is not an IP address.
		// let's try to resolve it
		memset(data.get(), 0, sizeof(struct Samurai::IO::Net::__InternalAddress));
		version = Unspecified;
		// FIXME: Maybe this is indeed a name? Perhaps we should look up a IPv6 name, when IPv6 is specified?
	}
	else
	{
		// printf("OK!\n\n\n");
		resolveState = ResolveState::Resolved;
	}
	
}


Samurai::IO::Net::InetAddress::InetAddress(const Samurai::IO::Net::InetAddress& address) : ResolveEventHandler(), version(Unspecified), data(nullptr), resolver(nullptr), resolveState(ResolveState::Unresolved), dnsevent(nullptr)
{
	version = address.version;
	data = std::make_unique<Samurai::IO::Net::__InternalAddress>();
	memcpy(data.get(), address.data.get(), sizeof(struct Samurai::IO::Net::__InternalAddress));
	hostname = address.hostname;
	resolveState = address.resolveState;
}


Samurai::IO::Net::InetAddress::InetAddress(const Samurai::IO::Net::InetAddress* address) : version(Unspecified), data(nullptr), resolver(nullptr), resolveState(ResolveState::Unresolved), dnsevent(nullptr)
{
	version = address->version;
	data = std::make_unique<Samurai::IO::Net::__InternalAddress>();
	memcpy(data.get(), address->data.get(), sizeof(struct Samurai::IO::Net::__InternalAddress));
	hostname = address->hostname;
	resolveState = address->resolveState;
}


Samurai::IO::Net::InetAddress::~InetAddress()
{
	/* The resolver holds this object as its event handler. Clearing dnsevent
	   before destroying it means a callback arriving during teardown cannot be
	   forwarded to a user handler that believes this address is still alive. */
	dnsevent = nullptr;

	resolver.reset();

	data.reset();
}


bool Samurai::IO::Net::InetAddress::setRawAddress(void* data_, size_t length, enum Samurai::IO::Net::InetAddress::Version ip_version)
{
	if (ip_version == Samurai::IO::Net::InetAddress::IPv4 && length < sizeof(struct in_addr)) return false;
	if (ip_version == Samurai::IO::Net::InetAddress::IPv6 && length < sizeof(struct in6_addr)) return false;
	
 	version = ip_version;
	memset(data.get(), 0, sizeof(struct Samurai::IO::Net::__InternalAddress));
	memcpy(data.get(), data_, length);
	resolveState = ResolveState::Resolved;
	return true;
}


uint32_t Samurai::IO::Net::InetAddress::getIPv4HostOrder() const
{
	if (version != IPv4 || !data) return 0;
	return ntohl(X_IP4_32);
}


bool Samurai::IO::Net::InetAddress::isValid() const
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


bool Samurai::IO::Net::InetAddress::isMulticast() const
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


bool Samurai::IO::Net::InetAddress::isPrivate() const
{
	if (version == IPv4) {
		const uint32_t host_order = getIPv4HostOrder();
		if ((host_order & 0xff000000) == 0x0a000000) return true; /* 10.0.0.0/8 */
		if ((host_order & 0xfff00000) == 0xac100000) return true; /* 172.16.0.0/12 */
		if ((host_order & 0xffff0000) == 0xc0a80000) return true; /* 192.168.0.0/16 */
		return false;
	} else if (version == IPv6) {
#ifdef SAMURAI_POSIX
		return ((X_IP6_08[0] & 0xfe) == 0xfc); /* fc00::/7 */
#else
		return false; // FIXME
#endif
	} else {
		return false;
	}
}


bool Samurai::IO::Net::InetAddress::isLinkLocal() const
{
	if (version == IPv4) {
		return ((getIPv4HostOrder() & 0xffff0000) == 0xa9fe0000); /* 169.254.0.0/16 */
	} else if (version == IPv6) {
#ifdef SAMURAI_POSIX
		return (X_IP6_08[0] == 0xfe && (X_IP6_08[1] & 0xc0) == 0x80); /* fe80::/10 */
#else
		return false; // FIXME
#endif
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


/* NOTE: formatted on demand and returned by value: a cached pointer would go
   stale when the address changed and dangle once the object died. */
std::string Samurai::IO::Net::InetAddress::getAddress() const
{
	if (resolveState != ResolveState::Resolved)
		return hostname;

	char buf[INET6_ADDRSTRLEN+1] = { 0, };
	const char* ret = nullptr;

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


/* operator!= is synthesised from this one. */
bool Samurai::IO::Net::InetAddress::operator==(const Samurai::IO::Net::InetAddress& copy) const
{
	if (&copy == this) return true;
	if (copy.version != version) return false;

	return memcmp(&data->internal, &copy.data->internal, sizeof(struct __InternalAddress)) == 0;
}

Samurai::IO::Net::InetAddress& Samurai::IO::Net::InetAddress::operator=(const std::string& address)
{
	data.reset();
	resolver.reset();
	
	version = Unspecified;
	data = std::make_unique<Samurai::IO::Net::__InternalAddress>();
	memset(data.get(), 0, sizeof(struct Samurai::IO::Net::__InternalAddress));
	
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
		memset(data.get(), 0, sizeof(struct Samurai::IO::Net::__InternalAddress));
		version = Unspecified;
	} else {
		resolveState = ResolveState::Resolved;
	}
	
	return *this;
}

Samurai::IO::Net::InetAddress& Samurai::IO::Net::InetAddress::operator=(const Samurai::IO::Net::InetAddress& copy)
{
	data.reset();
	resolver.reset();


	version = copy.version;
	data = std::make_unique<Samurai::IO::Net::__InternalAddress>();
	memcpy(data.get(), copy.data.get(), sizeof(struct Samurai::IO::Net::__InternalAddress));
	hostname = copy.hostname;
	resolveState = copy.resolveState;
	return *this;
}



bool Samurai::IO::Net::InetAddress::isResolved() const
{
	if (isValid() && resolveState == ResolveState::Resolved) return true;
	return false;
}


void Samurai::IO::Net::InetAddress::EventHostFound(const Samurai::IO::Net::InetAddress* address)
{
	if (!address || !address->data) return;

	/* Every constructor allocates 'data', so there is always a block to copy
	   into. */
	version = address->version;
	memcpy(data.get(), address->data.get(), sizeof(struct Samurai::IO::Net::__InternalAddress));
	resolveState = ResolveState::Resolved;

	/* Cleared before the callback, not after: the handler is free to destroy
	   this object, and coming back to touch a member afterwards would be a
	   use-after-free. */
	Samurai::IO::Net::ResolveEventHandler* handler = dnsevent;
	dnsevent = nullptr;

	if (handler) handler->EventHostFound(this);
}


void Samurai::IO::Net::InetAddress::EventHostError(enum Samurai::IO::Net::DNS::Resolver::Error error)
{
	resolveState = ResolveState::ResolveError;
	if (dnsevent) {
		dnsevent->EventHostError(error);
	}
	dnsevent = nullptr;
}


void Samurai::IO::Net::InetAddress::lookup(ResolveEventHandler* eventHandler)
{
	if (eventHandler) dnsevent = eventHandler;

	if (resolveState == ResolveState::Resolved)
	{
		Samurai::IO::Net::ResolveEventHandler* handler = dnsevent;
		dnsevent = nullptr;
		if (handler) handler->EventHostFound(this);
		return;
	}

	resolver.reset();

	resolver = Samurai::IO::Net::DNS::Resolver::getHostByName(this, hostname.c_str()); /* FIXME: std::string-ify */
}


/*
 * A dotted-quad written with a leading zero in any octet is ambiguous:
 * inet_aton() and a number of URL parsers read the zero as introducing an
 * octal number, so "010.1.1.1" is 10.1.1.1 to some readers and 8.1.1.1 to
 * others. Accepting it would mean disagreeing with whatever else inspects the
 * same address, so it is refused.
 *
 * This cannot be left to inet_pton(), which does not agree with itself across
 * platforms: glibc rejects the notation, macOS and the BSDs quietly read it as
 * decimal, and WSAStringToAddress() is more permissive still.
 */
static bool is_canonical_dotted_quad(const char* text)
{
	size_t digits = 0;
	size_t octets = 1;

	for (const char* p = text; *p; p++)
	{
		if (*p == '.')
		{
			if (!digits) return false;
			digits = 0;
			octets++;
			continue;
		}

		if (*p < '0' || *p > '9') return false;

		/* A second digit in an octet that began with a zero. */
		if (digits == 1 && *(p - 1) == '0') return false;

		if (++digits > 3) return false;
	}

	return digits > 0 && octets == 4;
}

/*
 * A dotted-quad can also appear inside an IPv6 literal, as in
 * "::ffff:1.2.3.4", where it is always the part following the last colon.
 */
static bool has_canonical_embedded_ipv4(const std::string& text)
{
	if (text.find('.') == std::string::npos) return true;

	std::string::size_type colon = text.rfind(':');
	return is_canonical_dotted_quad(text.c_str() + (colon == std::string::npos ? 0 : colon + 1));
}

bool Samurai::IO::Net::InetAddress::stringToAddress(enum Samurai::IO::Net::InetAddress::Version version, const char* address, struct __InternalAddress* data)
{
	if (!address || !*address || !data) return false;

	std::string text(address);

	/* Accept the bracketed form used in URLs and by toString(). */
	if (text.size() > 2 && text.front() == '[' && text.back() == ']')
		text = text.substr(1, text.size() - 2);

	if (version == Samurai::IO::Net::InetAddress::IPv4)
	{
		if (!is_canonical_dotted_quad(text.c_str())) return false;
		return net_string_to_address(AF_INET, text.c_str(), (void*) &data->internal.in) > 0;
	}

	if (version == Samurai::IO::Net::InetAddress::IPv6)
	{
		if (!has_canonical_embedded_ipv4(text)) return false;
		return net_string_to_address(AF_INET6, text.c_str(), (void*) &data->internal.in6) > 0;
	}

	return false;
}
