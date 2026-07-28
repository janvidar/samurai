/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/net/dns/common.h>
#include <samurai/io/net/dns/dnsmessage.h>
#include <samurai/io/net/dns/dnsrrs.h>

/* ------------------------------------------------------------------------- */
/* Header defaults                                                           */
/* ------------------------------------------------------------------------- */

EXO_TEST(dns_header_defaults_zero,
{
	Samurai::IO::Net::DNS::MessageHeader h;
	return h.id == 0 && h.flags_u16 == 0 && h.qdcount == 0
		&& h.ancount == 0 && h.nscount == 0 && h.arcount == 0;
});

EXO_TEST(dns_typeclass_defaults_invalid,
{
	Samurai::IO::Net::DNS::TypeClass tc;
	return tc.rr_type == Samurai::IO::Net::DNS::Type_Invalid
		&& tc.rr_class == Samurai::IO::Net::DNS::Class_Invalid;
});

EXO_TEST(dns_resourcerecord_defaults,
{
	Samurai::IO::Net::DNS::ResourceRecord rec;
	return rec.ttl == 0 && rec.rdLength == 0 && rec.rr == nullptr
		&& rec.name != nullptr;
});

/* ------------------------------------------------------------------------- */
/* MessageHeader::isValid()                                                  */
/*                                                                           */
/* The opcode must be masked off before it is shifted down. Folding the two   */
/* into one expression tests the response code instead, which rejects the     */
/* valid codes 3, 4 and 5.                                                   */
/* ------------------------------------------------------------------------- */

static bool dns_flags_valid(uint16_t flags)
{
	Samurai::IO::Net::DNS::MessageHeader h;
	h.flags_u16 = flags;
	return h.isValid();
}

EXO_TEST(dns_isvalid_plain_query,
{
	return dns_flags_valid(0x0000);
});

EXO_TEST(dns_isvalid_response_ok,
{
	/* response, recursion desired + available, rcode 0 */
	return dns_flags_valid(0x8180);
});

EXO_TEST(dns_isvalid_rcode_1_format_error,
{
	return dns_flags_valid(0x8181);
});

EXO_TEST(dns_isvalid_rcode_2_server_error,
{
	return dns_flags_valid(0x8182);
});

/* NXDOMAIN. This is the case the precedence bug rejected. */
EXO_TEST(dns_isvalid_rcode_3_name_error,
{
	return dns_flags_valid(0x8183);
});

EXO_TEST(dns_isvalid_rcode_4_not_implemented,
{
	return dns_flags_valid(0x8184);
});

EXO_TEST(dns_isvalid_rcode_5_refused,
{
	return dns_flags_valid(0x8185);
});

EXO_TEST(dns_isvalid_rejects_rcode_6,
{
	return !dns_flags_valid(0x8186);
});

EXO_TEST(dns_isvalid_accepts_opcode_1_iquery,
{
	/* opcode 1 sits in bits 11-14 */
	return dns_flags_valid(0x0800);
});

EXO_TEST(dns_isvalid_accepts_opcode_2_status,
{
	return dns_flags_valid(0x1000);
});

EXO_TEST(dns_isvalid_rejects_opcode_3,
{
	return !dns_flags_valid(0x1800);
});

/* The three 'z' bits are reserved and must be zero. */
EXO_TEST(dns_isvalid_rejects_nonzero_z,
{
	return !dns_flags_valid(0x0010) && !dns_flags_valid(0x0020)
		&& !dns_flags_valid(0x0040);
});

/* ------------------------------------------------------------------------- */
/* Header flag accessors                                                     */
/* ------------------------------------------------------------------------- */

EXO_TEST(dns_header_query_vs_response,
{
	Samurai::IO::Net::DNS::MessageHeader q;
	q.flags_u16 = 0x0000;
	Samurai::IO::Net::DNS::MessageHeader r;
	r.flags_u16 = 0x8000;
	return q.isQuery() && !q.isResponse() && r.isResponse() && !r.isQuery();
});

EXO_TEST(dns_header_flag_bits,
{
	Samurai::IO::Net::DNS::MessageHeader h;
	h.flags_u16 = 0x8780; /* response, aa, tc, rd, ra */
	return h.isAuthorative() && h.isTruncated()
		&& h.isRecursionDesired() && h.isRecursionAvailable();
});

EXO_TEST(dns_header_flag_bits_clear,
{
	Samurai::IO::Net::DNS::MessageHeader h;
	h.flags_u16 = 0x8000; /* response only */
	return !h.isAuthorative() && !h.isTruncated()
		&& !h.isRecursionDesired() && !h.isRecursionAvailable();
});

EXO_TEST(dns_header_response_code,
{
	Samurai::IO::Net::DNS::MessageHeader h;
	h.flags_u16 = 0x8183;
	return h.getResponseCode() == Samurai::IO::Net::DNS::DNS_STATUS_NAME_ERROR;
});

EXO_TEST(dns_header_query_type,
{
	Samurai::IO::Net::DNS::MessageHeader h;
	h.flags_u16 = 0x0000;
	return h.getQueryType() == Samurai::IO::Net::DNS::DNS_QT_QUERY;
});
