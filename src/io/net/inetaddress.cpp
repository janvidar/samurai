/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/socketglue.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/inetaddress.h>

#include <stdlib.h>

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


Samurai::IO::Net::InetAddress::InetAddress() : version(Unspecified), data(0), resolver(0), resolveState(Unresolved), dnsevent(0), string(0)
{
	data = new Samurai::IO::Net::__InternalAddress();
	memset(data, 0, sizeof(struct Samurai::IO::Net::__InternalAddress));
}

Samurai::IO::Net::InetAddress::InetAddress(enum Version ip_version) : version(ip_version), data(0), resolver(0), resolveState(Unresolved), dnsevent(0), string(0)
{
	data = new Samurai::IO::Net::__InternalAddress();
	memset(data, 0, sizeof(struct Samurai::IO::Net::__InternalAddress));
}

Samurai::IO::Net::InetAddress::InetAddress(const std::string& address, enum Version ip_version) : version(Unspecified), data(0), resolver(0), resolveState(Unresolved), dnsevent(0), string(0)
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
			ok = stringToAddress(IPv4, (char*) address.c_str(), data);
			if (ok)
			{
				// printf("Unspec is OK - IPv4\n");
				version = IPv4;
			}
			else
			{
				ok = stringToAddress(IPv6, (char*) address.c_str(), data);
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
				ok = stringToAddress(IPv4, (char*) address.c_str(), data);
			}
			else if (version == IPv6)
			{
				// printf("Specified IPv6\n");
				ok = stringToAddress(IPv6, (char*) address.c_str(), data);
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


Samurai::IO::Net::InetAddress::InetAddress(const Samurai::IO::Net::InetAddress& address) : ResolveEventHandler(), version(Unspecified), data(0), resolver(0), resolveState(Unresolved), dnsevent(0), string(0)
{
	version = address.version;
	data = new Samurai::IO::Net::__InternalAddress();
	memcpy(data, address.data, sizeof(struct Samurai::IO::Net::__InternalAddress));
	hostname = address.hostname;
	resolveState = address.resolveState;
}


Samurai::IO::Net::InetAddress::InetAddress(const Samurai::IO::Net::InetAddress* address) : version(Unspecified), data(0), resolver(0), resolveState(Unresolved), dnsevent(0), string(0)
{
	version = address->version;
	data = new Samurai::IO::Net::__InternalAddress();
	memcpy(data, address->data, sizeof(struct Samurai::IO::Net::__InternalAddress));
	hostname = address->hostname;
	resolveState = address->resolveState;
}


Samurai::IO::Net::InetAddress::~InetAddress()
{
	delete data;
	delete resolver;
	delete[] string;
}


bool Samurai::IO::Net::InetAddress::setRawAddress(void* data_, size_t length, enum Samurai::IO::Net::InetAddress::Version ip_version)
{
	if (ip_version == Samurai::IO::Net::InetAddress::IPv4 && length < sizeof(struct in_addr)) return false;
	if (ip_version == Samurai::IO::Net::InetAddress::IPv6 && length < sizeof(struct in6_addr)) return false;
	
	if (string) { delete[] string; string = 0; }
	
 	version = ip_version;
	memset(data, 0, sizeof(struct Samurai::IO::Net::__InternalAddress));
	memcpy(data, data_, length);
	resolveState = Resolved;
	return true;
}


bool Samurai::IO::Net::InetAddress::isValid()
{
	if (version == IPv4) {
#ifdef SAMURAI_POSIX
		return X_IP4_32 && !IN_BADCLASS(X_IP4_32) && !IN_EXPERIMENTAL(X_IP4_32);
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
		return (ntohl(X_IP4_32) >= 0xe0000000 && ntohl(X_IP4_32) < 0xf0000000);
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
		return ((ntohl(X_IP4_32) & 0xff000000) == 0x7f000000);
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


const char* Samurai::IO::Net::InetAddress::getAddress() const
{
	if (resolveState != Resolved)
	{
		return hostname.c_str();
	}
	
	if (string) return string;
	
	string = new char[INET6_ADDRSTRLEN+1];
	memset(string, 0, INET6_ADDRSTRLEN);
	
	const char* ret = 0;

	if (version == IPv4) {
		ret = net_address_to_string(AF_INET, (void*) &data->internal.in, string, INET_ADDRSTRLEN);
	} else if (version == IPv6) {
		ret = net_address_to_string(AF_INET6, (void*) &data->internal.in6, string, INET6_ADDRSTRLEN);
	}

	if (!ret)
	{
		fprintf(stderr, "Unable to convert to string!\n");
		return 0;
	}
	return string;
}


const char* Samurai::IO::Net::InetAddress::toString() const
{
	const char* string = getAddress();
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
	delete[] string; string = 0;
	
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
	delete[] string; string = 0;


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
	if (string) { delete[] string; string = 0; }
	version = address->version;
	data = new Samurai::IO::Net::__InternalAddress();
	memcpy(data, address->data, sizeof(struct Samurai::IO::Net::__InternalAddress));
	resolveState = Resolved;
	
	if (dnsevent) {
		dnsevent->EventHostFound(this);
	}
	dnsevent = 0;
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
	
	if (resolveState != Resolved)
	{
		resolver = Samurai::IO::Net::DNS::Resolver::getHostByName(this, hostname.c_str()); /* FIXME: std::string-ify */
	}
	else
	{
		if (eventHandler)
		{
			eventHandler->EventHostFound(this);	
		}
	}
}


/* This will return -1 if no number is found */
static int getNumber(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	return -1;
}

static int getHexNumber(char c)
{
	int ret = -1;
	if (c >= '0' && c <= '9')
		ret = c - '0';
	if (ret == -1 && c >= 'a' && c <= 'f')
		ret = (c - 'a') + 10;
	if (ret == -1 && c >= 'A' && c <= 'F')
		ret = (c - 'A') + 10;
		
	// QDBG("GetHexNum for '%c' => %d", c, ret);
	return ret;
}

#define RETURN_ERROR \
{ \
	return false; \
}


bool Samurai::IO::Net::InetAddress::stringToAddress(enum Samurai::IO::Net::InetAddress::Version version, char* address, struct __InternalAddress* data)
{
	if (!address || !strlen(address) || !data) return false;
	
	char* p = address;
	
	int adata[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	int max_value = 65535;
	int pos      = 0; /* offset into adata */
	int last_pos = 7;
	char separator = ':';
	
	enum { Invalid, Start, Data, Separator, End } parse_state = Invalid;
	
	if (version == Samurai::IO::Net::InetAddress::IPv4)
	{
		if (strlen(address) < 6) RETURN_ERROR;
		
		char separator = '.';
		last_pos = 3;
		max_value = 255;
		
		parse_state = Start;
		while (*p)
		{
			char ch = p[0];
			if (getNumber(ch) != -1)
			{
				parse_state = Data;
				if (adata[pos]) adata[pos] *= 10;
				adata[pos] += getNumber(ch);
				if (adata[pos] > max_value) {
					RETURN_ERROR;
				}
			}
			else if (ch == separator)
			{
				if (parse_state != Data) RETURN_ERROR;
				pos++;
				parse_state = Separator;
				if (pos > last_pos) RETURN_ERROR;
			}
			else
			{
				return false;
			}
			p++;
		}
		if (pos < last_pos) RETURN_ERROR;

		data->internal.in.s_addr = htonl((uint32_t) ((adata[0] << 24) | (adata[1] << 16) | (adata[2] << 8) | (adata[3])));
		
		return true;
		
	}
	else
	if (version == Samurai::IO::Net::InetAddress::IPv6)
	{
		if (strlen(address) < 2) RETURN_ERROR;
		bool compress_found = false;
		
//		QDBG("Samurai::IO::Net::InetAddress::stringToAddress: version=%s, address=%s", version == IPv4 ? "ipv4" : "ipv6", address);
		
		if (strncasecmp("::ffff:", address, 7) == 0 && strlen(address) > 13)
		{
			/* IPv4 mapped address */
			return stringToAddress(version, &address[7], data);
		}
		
		
		while (*p)
		{
			char ch = p[0];
			
			if (ch == '[' && version == IPv6) {
				if (parse_state != Invalid) RETURN_ERROR;
				parse_state = Start;
			
			} else if (ch == ']' && version == IPv6) {
				parse_state = End;
				break;
			
			} else if (getHexNumber(ch) != -1)
			{
				parse_state = Data;
				if (adata[pos]) adata[pos] *= 16;
				adata[pos] += getHexNumber(ch);
				if (adata[pos] > max_value)
					RETURN_ERROR;
			}
			else if (ch == separator)
			{
				if (parse_state == Separator || parse_state == Invalid || parse_state == Start)
				{
					if (compress_found && parse_state == Separator) RETURN_ERROR;
					compress_found = true;
					
					int pad = 0;
					char* left = p;
					while ((left = strchr(&left[1], separator))) pad++;
					/*
					left = p;
					if ((left = strchr(&left[1], '.')))
					{
						QDBG("Mixed mode configuration");
					}
					*/
					pos = last_pos - pad;
					
					
				}
				else {
					pos++;
					parse_state = Separator;
					if (pos > last_pos) RETURN_ERROR;
				}
			}
			else
			{
				// QERR("Unexpected: '%c'", ch);
				RETURN_ERROR;
			}
			p++;
		}
		
		if (pos < last_pos) RETURN_ERROR;
		
/*
		QDBG("deconvert: %x:%x:%x:%x:%x:%x:%x:%x",	adata[0], adata[1], adata[2], adata[3],
													adata[4], adata[5], adata[6], adata[7]);
*/
		
		for (int i = 0; i < 8; i++)
			X_IP6_16[i] = ntohs(adata[i]);
		
		return true;
		
	}


	free(address);
	return false;
}
