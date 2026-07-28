/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/net/dns/common.h>
#include <samurai/io/net/dns/dnsmessage.h>
#include <samurai/io/net/dns/dnsrrs.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/buffer.h>

/*
 * Wire fixtures. These live at file scope because EXO_TEST takes two macro
 * arguments and the preprocessor would split a braced initialiser containing
 * commas.
 */

/* Header only: id, flags, qdcount, ancount, nscount, arcount.
 *
 * The flag word is 0x0080 - recursion available, response code 0. Its high
 * byte is zero and its low byte has the top bit set, which is what catches a
 * decoder that sign-extends: reading it through a signed char yields 0xff80,
 * whose opcode field is 15 and which is therefore rejected as malformed. */
static const char dns_header_ra_only[] = {
	'\x12', '\x34',
	'\x00', '\x80',
	'\x00', '\x00',
	'\x00', '\x00',
	'\x00', '\x00',
	'\x00', '\x00'
};

/* One answer record: name "a.", type A, class IN, ttl 256, 4 bytes of 127.0.0.1 */
static const char dns_answer_a[] = {
	'\x12', '\x34',
	'\x81', '\x80',
	'\x00', '\x00',
	'\x00', '\x01',
	'\x00', '\x00',
	'\x00', '\x00',
	'\x01', 'a', '\x00',
	'\x00', '\x01',
	'\x00', '\x01',
	'\x00', '\x00', '\x01', '\x00',
	'\x00', '\x04',
	'\x7f', '\x00', '\x00', '\x01'
};

/* Same, but the A record claims three bytes of address data. Decoding it must
 * fail rather than build an address from partly undefined bytes. */
static const char dns_answer_a_short[] = {
	'\x12', '\x34',
	'\x81', '\x80',
	'\x00', '\x00',
	'\x00', '\x01',
	'\x00', '\x00',
	'\x00', '\x00',
	'\x01', 'a', '\x00',
	'\x00', '\x01',
	'\x00', '\x01',
	'\x00', '\x00', '\x01', '\x00',
	'\x00', '\x03',
	'\x7f', '\x00', '\x00'
};

/* Too short to hold even the fixed header. */
static const char dns_truncated[] = {
	'\x12', '\x34', '\x00', '\x80', '\x00', '\x00'
};

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

/* ------------------------------------------------------------------------- */
/* Message::decode()                                                         */
/* ------------------------------------------------------------------------- */

/*
 * A flag word whose low byte has the top bit set must survive the decode. A
 * decoder that builds the value out of Buffer::at() - which returns a signed
 * char - sign-extends that byte across the whole word and turns 0x0080 into
 * 0xff80, which isValid() then rejects.
 */
EXO_TEST(dns_decode_header_does_not_sign_extend,
{
	Samurai::IO::Buffer buf;
	buf.append(dns_header_ra_only, sizeof(dns_header_ra_only));
	Samurai::IO::Net::DNS::Message msg(&buf);
	return msg.decode() == Samurai::IO::Net::DNS::DNS_STATUS_OK;
});

/* 0x0080 has bit 0x8000 clear, so this is a query. Sign extension would set
 * the high bits and make it read as a response. */
EXO_TEST(dns_decode_header_flag_high_bits_stay_clear,
{
	Samurai::IO::Buffer buf;
	buf.append(dns_header_ra_only, sizeof(dns_header_ra_only));
	Samurai::IO::Net::DNS::Message msg(&buf);
	msg.decode();
	return !msg.isResponse();
});

EXO_TEST(dns_decode_truncated_header_fails,
{
	Samurai::IO::Buffer buf;
	buf.append(dns_truncated, sizeof(dns_truncated));
	Samurai::IO::Net::DNS::Message msg(&buf);
	return msg.decode() == Samurai::IO::Net::DNS::DNS_STATUS_FORMAT_ERROR;
});

EXO_TEST(dns_decode_no_buffer_fails,
{
	Samurai::IO::Net::DNS::Message msg;
	return msg.decode() == Samurai::IO::Net::DNS::DNS_STATUS_FORMAT_ERROR;
});

EXO_TEST(dns_decode_answer_a_ok,
{
	Samurai::IO::Buffer buf;
	buf.append(dns_answer_a, sizeof(dns_answer_a));
	Samurai::IO::Net::DNS::Message msg(&buf);
	return msg.decode() == Samurai::IO::Net::DNS::DNS_STATUS_OK
		&& msg.isResponse();
});

EXO_TEST(dns_decode_answer_a_yields_one_record,
{
	Samurai::IO::Buffer buf;
	buf.append(dns_answer_a, sizeof(dns_answer_a));
	Samurai::IO::Net::DNS::Message msg(&buf);
	msg.decode();
	return msg.getRecord((size_t) 0) != 0 && msg.getRecord((size_t) 1) == 0;
});

EXO_TEST(dns_decode_answer_a_type_and_ttl,
{
	Samurai::IO::Buffer buf;
	buf.append(dns_answer_a, sizeof(dns_answer_a));
	Samurai::IO::Net::DNS::Message msg(&buf);
	msg.decode();
	Samurai::IO::Net::DNS::ResourceRecord* rec = msg.getRecord((size_t) 0);
	if (!rec) return false;
	return rec->type_class.rr_type == Samurai::IO::Net::DNS::Type_A
		&& rec->type_class.rr_class == Samurai::IO::Net::DNS::Class_IN
		&& rec->ttl == 256
		&& rec->rdLength == 4;
});

EXO_TEST(dns_decode_answer_a_address,
{
	Samurai::IO::Buffer buf;
	buf.append(dns_answer_a, sizeof(dns_answer_a));
	Samurai::IO::Net::DNS::Message msg(&buf);
	msg.decode();
	Samurai::IO::Net::DNS::ResourceRecord* rec = msg.getRecord((size_t) 0);
	if (!rec) return false;
	Samurai::IO::Net::DNS::RR_A* a =
		dynamic_cast<Samurai::IO::Net::DNS::RR_A*>(rec->rr);
	if (!a || !a->getAddress()) return false;
	return a->getAddress()->toString() == "127.0.0.1";
});

/* An A record has to be exactly four bytes. A three byte one used to be
 * accepted and read a fourth byte from beyond its own record data. */
EXO_TEST(dns_decode_short_a_record_rejected,
{
	Samurai::IO::Buffer buf;
	buf.append(dns_answer_a_short, sizeof(dns_answer_a_short));
	Samurai::IO::Net::DNS::Message msg(&buf);
	return msg.decode() == Samurai::IO::Net::DNS::DNS_STATUS_FORMAT_ERROR;
});
