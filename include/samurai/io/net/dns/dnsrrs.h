/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SYSTEM_DNS_RRS_H
#define HAVE_SYSTEM_DNS_RRS_H

#include <samurai/io/net/dns/dnsutil.h>
#include <samurai/io/net/dns/common.h>
#include <string>
#include <time.h>

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
		~ResourceRecord();

		/**
		 * A record is only expirable once it has been stamped; an unstamped
		 * one - anything that has not been through a cache - never expires.
		 */
		bool isExpired() const;
		int32_t getTimeToLive() const { return ttl; }

		/**
		 * Start the record's lifetime now. A TTL is counted from the moment
		 * the response arrived, so this is called on the way into the cache
		 * rather than during decoding.
		 */
		void stampExpiry();

		time_t getExpiryTime() const { return expireTime; }

	public:
		Name* name = nullptr;
		TypeClass type_class;
		int32_t ttl = 0;
		uint16_t rdLength = 0;
		RR* rr = nullptr;

		/* Absolute time this record stops being usable; 0 until stamped. */
		time_t expireTime = 0;
};



class RR_SOA final : public RR {
	public:
		RR_SOA(const Name& sone, const Name& email, uint32_t serial, uint32_t refresh, uint32_t retry, uint32_t expire, int32_t ttl);
		~RR_SOA() override;
		
	protected:
		Name* primary;
		Name* email;
		uint32_t serial;
		uint32_t refresh;
		uint32_t retry;
		uint32_t expire;
		int32_t ttl;
};


class RR_CNAME final : public RR {
	public:
		RR_CNAME(const Name& name);
		~RR_CNAME() override;
		
		Name* getName() { return name; }
		
	protected:
		Name* name;
};


class RR_PTR final : public RR {
	public:
		RR_PTR(const Name& name);
		~RR_PTR() override;
		
		Name* getName() { return name; }
		
	protected:
		Name* name;
};


class RR_NS final : public RR {
	public:
		RR_NS(const Name& name);
		~RR_NS() override;
		
		Name* getName() { return name; }
		
	protected:
		Name* name;
};


class RR_A final : public RR {
	public:
		RR_A(const InetAddress& addr);
		~RR_A() override;
		
		InetAddress* getAddress();
		
	protected:
		InetAddress* addr;
};


class RR_AAAA final : public RR {
	public:
		RR_AAAA(const InetAddress& addr);
		~RR_AAAA() override;
		
		InetAddress* getAddress();
		
	protected:
		InetAddress* addr;
};


class RR_TXT final : public RR {
	public:
		RR_TXT(const char* txt);
		~RR_TXT() override;

		const std::string& getText() const { return txt; }

	protected:
		std::string txt;
};

} // namespace DNS
} // namespace Net
} // namespace IO
} // namespace Samurai

#endif // HAVE_SYSTEM_DNS_RRS_H
