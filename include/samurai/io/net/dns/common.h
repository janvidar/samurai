/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_DNS_RESOLVER_COMMONS_H
#define HAVE_DNS_RESOLVER_COMMONS_H

#include <stdint.h>

#define DNS_SERVER_PORT         53

#define DNS_LABEL_SIZE          63
#define DNS_NAME_SIZE           255
#define DNS_MAX_PACKET_SIZE     512 /* A UDP message is limited to 512 bytes */

#define DNS_CACHE_NEGATIVE_TTL  600 /* 10 minutes */
#define DNS_CACHE_MAX_STORAGE   64  /* Store up to this number of entries in cache */

#define DNS_RECURSE_MAX         10 /* most 10 recursions allowed for aliases (CNAME) */

/*
 * MAXNS and RES_TIMEOUT would come from <resolv.h> on the platforms that have
 * it, but MAXNS bounds a public member array, so taking it from a system
 * header would make the layout of ResolveConfiguration vary by platform.
 * These are the values every build has actually used.
 */
#ifndef MAXNS
#define MAXNS 3
#endif

#ifndef RES_TIMEOUT
#define RES_TIMEOUT 5
#endif


namespace Samurai {
namespace IO {
namespace Net {
namespace DNS {

enum class Type : uint16_t {
	A       = 1,  /* a host address */
	NS      = 2,  /* an authoritative name server */
	MD      = 3,  /* a mail destination (Obsolete - use MX) */
	MF      = 4,  /* a mail forwarder (Obsolete - use MX) */
	CNAME   = 5,  /* the canonical name for an alias */
	SOA     = 6,  /* marks the start of a zone of authority */
	MB      = 7,  /* a mailbox domain name (EXPERIMENTAL) */
	MG      = 8,  /* a mail group member (EXPERIMENTAL) */
	MR      = 9,  /* a mail rename domain name (EXPERIMENTAL) */
	NullRR  = 10, /* a null RR (EXPERIMENTAL) */
	WKS     = 11, /* a well known service description */
	PTR     = 12, /* a domain name pointer */
	HINFO   = 13, /* host information */
	MINFO   = 14, /* mailbox or mail list information */
	MX      = 15, /* mail exchange */
	TXT     = 16, /* text strings */

	/* defined in RFC 1183 */
	RP      = 17,
	AFSDB   = 18,
	X25     = 19,
	ISDN    = 20,
	RT      = 21,

	/* defined in RFC 1348, revised in RFC 1637 */
	NSAP    = 22,
	NSAPPTR = 23,

	/* reserved in RFC 1700, defined in RFC 2065, revised in RFC 2535 */
	SIG     = 24,
	KEY     = 25,

	/* defined in RFC 1664, updated by RFC 2163 */
	PX      = 26,

	/* defined in RFC 1712, already withdrawn */
	GPOS    = 27,

	/* reserved in RFC 1700, defined in RFC 1884 and 1886 */
	AAAA    = 28,

	/* defined in RFC 1876 */
	LOC     = 29,

	/* defined in RFC 2065, revised in RFC 2535 */
	NXT     = 30,

	/* defined in RFC XXXX */
	EID     = 31,

	/* defined in RFC XXXX */
	NIMLOC  = 32,

	/* defined in RFC 2052, updated by RFC 2782 */
	SRV     = 33,

	/* defined in RFC XXXX */
	ATMA    = 34,

	/* defined in RFC 2168 */
	NAPTR   = 35,

	/* defined in RFC 2230 */
	KX      = 36,

	/* defined in RFC 2538 */
	CERT    = 37,

	/* defined in RFC XXXX */
	A6      = 38,

	/* defined in RFC XXXX */
	DNAME   = 39,

	/* defined in RFC XXXX */
	SINK    = 40,

	/* defined in RFC 2671 */
	OPT     = 41,

	/* Old/deprecated types */
	UINFO   = 100,
	UID     = 101,
	GID     = 102,
	UNSPEC  = 103,
	ADDRS   = 248,
	TKEY    = 249,
	TSIG    = 250,
	
	/* defined in RFC 1995 */
	IXFR    = 251,
	AXFR    = 252,

	/* obsolete/deprecated types already missing on some platforms */
	MAILB   = 253,
	MAILA   = 254,

	Invalid      = 0xffff
};
		
enum class Class : uint16_t {
	IN  = 1, /* the Internet */
	CS  = 2, /* the CSNET class (Obsolete - used only for examples in some obsolete RFCs) */
	CH  = 3, /* the CHAOS class */
	HS  = 4, /* Hesiod [Dyer 87] */
	Invalid   = 0xffff
};

class TypeClass {
	public:
		Type  rr_type  = Type::Invalid;
		Class rr_class = Class::Invalid;
};

enum class QueryType
{
	Query,               /* standard query */
	InverseQuery,              /* inverse query */
	Status,              /* server status request */
	Reserved             /* reserved for future use */
};

enum class ResponseCode
{
	Ok,
	FormatError,    /* server is unable to interpret the query */
	ServerError,    /* server is unable to process request */
	NameError,      /* name does not exist */
	NotImplemented, /* the name server does not support this query */
	Refused,         /* the server refuses to answer this query */
	Reserved,        /* reserved for future use */

	/* NOTE: These are client side status codes, not part of any RFC. */
	Truncated,       /* the RR was truncated, need to retry with TCP */
	RecurseError    /* too many levels of recursions (CNAME) */

	
};


}
}
}
}

#endif // HAVE_DNS_RESOLVER_COMMONS_H
