/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/dns/dnsrrs.h>
#include <samurai/io/net/inetaddress.h>

Samurai::IO::Net::DNS::ResourceRecord::ResourceRecord() = default;
Samurai::IO::Net::DNS::ResourceRecord::~ResourceRecord() = default;

void Samurai::IO::Net::DNS::ResourceRecord::stampExpiry() {
	/* A negative TTL would put the expiry in the past, which is the same as
	   asking for the record not to be cached at all. */
	expireTime = time(0) + (ttl > 0 ? (time_t) ttl : 0);
}

bool Samurai::IO::Net::DNS::ResourceRecord::isExpired() const {
	if (!expireTime) return false;
	return time(0) >= expireTime;
}

Samurai::IO::Net::DNS::RR::RR()
{

}

Samurai::IO::Net::DNS::RR::~RR() {

}



Samurai::IO::Net::DNS::RR_SOA::RR_SOA(const Samurai::IO::Net::DNS::Name& sone_, const Samurai::IO::Net::DNS::Name& email_, uint32_t serial_, uint32_t refresh_, uint32_t retry_, uint32_t expire_, int32_t ttl_)
{
	primary = sone_;
	email = email_;
	serial = serial_;
	refresh = refresh_;
	retry = retry_;
	expire = expire_;
	ttl = ttl_;
	
}

Samurai::IO::Net::DNS::RR_SOA::~RR_SOA() = default;

Samurai::IO::Net::DNS::RR_CNAME::RR_CNAME(const Samurai::IO::Net::DNS::Name& name_)
	: name(name_)
{
}

Samurai::IO::Net::DNS::RR_CNAME::~RR_CNAME() = default;

Samurai::IO::Net::DNS::RR_PTR::RR_PTR(const Samurai::IO::Net::DNS::Name& name_)
	: name(name_)
{
}

Samurai::IO::Net::DNS::RR_PTR::~RR_PTR() = default;

Samurai::IO::Net::DNS::RR_NS::RR_NS(const Samurai::IO::Net::DNS::Name& name_)
	: name(name_)
{
}

Samurai::IO::Net::DNS::RR_NS::~RR_NS() = default;

Samurai::IO::Net::DNS::RR_A::RR_A(const Samurai::IO::Net::InetAddress& addr_)
	: addr(addr_)
{
}

Samurai::IO::Net::DNS::RR_A::~RR_A() = default;

const Samurai::IO::Net::InetAddress* Samurai::IO::Net::DNS::RR_A::getAddress() const
{
	return &addr;
}

Samurai::IO::Net::DNS::RR_AAAA::RR_AAAA(const Samurai::IO::Net::InetAddress& addr_)
	: addr(addr_)
{
}

Samurai::IO::Net::DNS::RR_AAAA::~RR_AAAA() = default;

const Samurai::IO::Net::InetAddress* Samurai::IO::Net::DNS::RR_AAAA::getAddress() const
{
	return &addr;
}


Samurai::IO::Net::DNS::RR_TXT::RR_TXT(const char* txt_)
{
	if (txt_)
		txt = txt_;
}

Samurai::IO::Net::DNS::RR_TXT::~RR_TXT()
{
}

