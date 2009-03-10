/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_DNS_RESOLVER_COMMONS_H
#define HAVE_DNS_RESOLVER_COMMONS_H

#define DNS_SERVER_PORT         53

#define DNS_LABEL_SIZE          63
#define DNS_NAME_SIZE           255
#define DNS_MAX_PACKET_SIZE     512 /* A UDP message is limited to 512 bytes */

#define DNS_CACHE_NEGATIVE_TTL  600 /* 10 minutes */
#define DNS_CACHE_MAX_STORAGE   64  /* Store up to this number of entries in cache */

#define DNS_RECURSE_MAX         10 /* most 10 recursions allowed for aliases (CNAME) */

#ifdef UNIX
#if !defined(MACOSX) && !defined(FREEBSD)
#include <resolv.h>
#endif
#endif

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

enum Type {
	Type_A       = 1,  /* a host address */
	Type_NS      = 2,  /* an authoritative name server */
	Type_MD      = 3,  /* a mail destination (Obsolete - use MX) */
	Type_MF      = 4,  /* a mail forwarder (Obsolete - use MX) */
	Type_CNAME   = 5,  /* the canonical name for an alias */
	Type_SOA     = 6,  /* marks the start of a zone of authority */
	Type_MB      = 7,  /* a mailbox domain name (EXPERIMENTAL) */
	Type_MG      = 8,  /* a mail group member (EXPERIMENTAL) */
	Type_MR      = 9,  /* a mail rename domain name (EXPERIMENTAL) */
	Type_NULL    = 10, /* a null RR (EXPERIMENTAL) */
	Type_WKS     = 11, /* a well known service description */
	Type_PTR     = 12, /* a domain name pointer */
	Type_HINFO   = 13, /* host information */
	Type_MINFO   = 14, /* mailbox or mail list information */
	Type_MX      = 15, /* mail exchange */
	Type_TXT     = 16, /* text strings */

	/* defined in RFC 1183 */
	Type_RP      = 17,
	Type_AFSDB   = 18,
	Type_X25     = 19,
	Type_ISDN    = 20,
	Type_RT      = 21,

	/* defined in RFC 1348, revised in RFC 1637 */
	Type_NSAP    = 22,
	Type_NSAPPTR = 23,

	/* reserved in RFC 1700, defined in RFC 2065, revised in RFC 2535 */
	Type_SIG     = 24,
	Type_KEY     = 25,

	/* defined in RFC 1664, updated by RFC 2163 */
	Type_PX      = 26,

	/* defined in RFC 1712, already withdrawn */
	Type_GPOS    = 27,

	/* reserved in RFC 1700, defined in RFC 1884 and 1886 */
	Type_AAAA    = 28,

	/* defined in RFC 1876 */
	Type_LOC     = 29,

	/* defined in RFC 2065, revised in RFC 2535 */
	Type_NXT     = 30,

	/* defined in RFC XXXX */
	Type_EID     = 31,

	/* defined in RFC XXXX */
	Type_NIMLOC  = 32,

	/* defined in RFC 2052, updated by RFC 2782 */
	Type_SRV     = 33,

	/* defined in RFC XXXX */
	Type_ATMA    = 34,

	/* defined in RFC 2168 */
	Type_NAPTR   = 35,

	/* defined in RFC 2230 */
	Type_KX      = 36,

	/* defined in RFC 2538 */
	Type_CERT    = 37,

	/* defined in RFC XXXX */
	Type_A6      = 38,

	/* defined in RFC XXXX */
	Type_DNAME   = 39,

	/* defined in RFC XXXX */
	Type_SINK    = 40,

	/* defined in RFC 2671 */
	Type_OPT     = 41,

	/* Old/deprecated types */
	Type_UINFO   = 100,
	Type_UID     = 101,
	Type_GID     = 102,
	Type_UNSPEC  = 103,
	Type_ADDRS   = 248,
	Type_TKEY    = 249,
	Type_TSIG    = 250,
	
	/* defined in RFC 1995 */
	Type_IXFR    = 251,
	Type_AXFR    = 252,

	/* obsolete/deprecated types already missing on some platforms */
	Type_MAILB   = 253,
	Type_MAILA   = 254,

	Type_Invalid = 0xffff /* ensure 16 bits */
};
		
enum Class {
	Class_IN  = 1, /* the Internet */
	Class_CS  = 2, /* the CSNET class (Obsolete - used only for examples in some obsolete RFCs) */
	Class_CH  = 3, /* the CHAOS class */
	Class_HS  = 4, /* Hesiod [Dyer 87] */
	Class_Invalid = 0xffff /* ensure 16 bits */
};

class TypeClass {
	public:
		uint16_t rr_type;
		uint16_t rr_class;

};

enum QueryType
{
	DNS_QT_QUERY,               /* standard query */
	DNS_QT_IQUERY,              /* inverse query */
	DNS_QT_STATUS,              /* server status request */
	DNS_QT_RESERVED             /* reserved for future use */
};

enum ResponseCode
{
	DNS_STATUS_OK,
	DNS_STATUS_FORMAT_ERROR,    /* server is unable to interpret the query */
	DNS_STATUS_SERVER_ERROR,    /* server is unable to process request */
	DNS_STATUS_NAME_ERROR,      /* name does not exist */
	DNS_STATUS_NOT_IMPLEMENTED, /* the name server does not support this query */
	DNS_STATUS_REFUSED,         /* the server refuses to answer this query */
	DNS_STATUS_RESERVED,        /* reserved for future use */

	/* NOTE: These are client side status codes, not part of any RFC. */
	DNS_STATUS_TRUNCATED,       /* the RR was truncated, need to retry with TCP */
	DNS_STATUS_RECURSE_ERROR    /* too many levels of recursions (CNAME) */

	
};


}
}
}
}

#endif // HAVE_DNS_RESOLVER_COMMONS_H
