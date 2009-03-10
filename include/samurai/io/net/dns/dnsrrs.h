/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SYSTEM_DNS_RRS_H
#define HAVE_SYSTEM_DNS_RRS_H

#include <samurai/io/net/dns/dnsutil.h>
#include <samurai/io/net/dns/common.h>

namespace Samurai {
namespace IO {
namespace Net {
class InetAddress;
namespace DNS {

class RR {
	public:
		RR();
		virtual ~RR();
};

class ResourceRecord {
	public:
		ResourceRecord();
		virtual ~ResourceRecord();

		bool isExpired() const;
		int32_t getTimeToLive() const { return ttl; }
		
	public:
		Name* name;
		TypeClass type_class;
		int32_t ttl;
		uint16_t rdLength;
		RR* rr;
};



class RR_SOA : public RR {
	public:
		RR_SOA(const Name& sone, const Name& email, uint32_t serial, uint32_t refresh, uint32_t retry, uint32_t expire, int32_t ttl);
		virtual ~RR_SOA();
		
	protected:
		Name* primary;
		Name* email;
		uint32_t serial;
		uint32_t refresh;
		uint32_t retry;
		uint32_t expire;
		int32_t ttl;
};


class RR_CNAME : public RR {
	public:
		RR_CNAME(const Name& name);
		virtual ~RR_CNAME();
		
		Name* getName() { return name; }
		
	protected:
		Name* name;
};


class RR_PTR : public RR {
	public:
		RR_PTR(const Name& name);
		virtual ~RR_PTR();
		
		Name* getName() { return name; }
		
	protected:
		Name* name;
};


class RR_NS : public RR {
	public:
		RR_NS(const Name& name);
		virtual ~RR_NS();
		
		Name* getName() { return name; }
		
	protected:
		Name* name;
};


class RR_A : public RR {
	public:
		RR_A(const InetAddress& addr);
		virtual ~RR_A();
		
		InetAddress* getAddress();
		
	protected:
		InetAddress* addr;
};


class RR_AAAA : public RR {
	public:
		RR_AAAA(const InetAddress& addr);
		virtual ~RR_AAAA();
		
		InetAddress* getAddress();
		
	protected:
		InetAddress* addr;
};


class RR_TXT : public RR {
	public:
		RR_TXT(const char* txt);
		virtual ~RR_TXT();

	protected:
		char* txt;
};

} // namespace DNS
} // namespace Net
} // namespace IO
} // namespace Samurai

#endif // HAVE_SYSTEM_DNS_RRS_H
