/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <memory>
#include <string>
#include <vector>
#include <samurai/io/net/dns/common.h>
#include <samurai/io/net/dns/dnsmessage.h>
#include <samurai/io/net/dns/dnsrrs.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/buffer.h>
#include <samurai/io/net/dns/cache.h>
#include <string.h>
#include <samurai/io/file.h>
#include <samurai/io/net/dns/dnsconfig.h>

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
	return tc.rr_type == Samurai::IO::Net::DNS::Type::Invalid
		&& tc.rr_class == Samurai::IO::Net::DNS::Class::Invalid;
});

EXO_TEST(dns_resourcerecord_defaults,
{
	Samurai::IO::Net::DNS::ResourceRecord rec;
	return rec.ttl == 0 && rec.rdLength == 0 && rec.rr == nullptr
		&& rec.name.countParts() == 0;
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
	return h.getResponseCode() == Samurai::IO::Net::DNS::ResponseCode::NameError;
});

EXO_TEST(dns_header_query_type,
{
	Samurai::IO::Net::DNS::MessageHeader h;
	h.flags_u16 = 0x0000;
	return h.getQueryType() == Samurai::IO::Net::DNS::QueryType::Query;
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
	return msg.decode() == Samurai::IO::Net::DNS::ResponseCode::Ok;
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
	return msg.decode() == Samurai::IO::Net::DNS::ResponseCode::FormatError;
});

EXO_TEST(dns_decode_no_buffer_fails,
{
	Samurai::IO::Net::DNS::Message msg;
	return msg.decode() == Samurai::IO::Net::DNS::ResponseCode::FormatError;
});

EXO_TEST(dns_decode_answer_a_ok,
{
	Samurai::IO::Buffer buf;
	buf.append(dns_answer_a, sizeof(dns_answer_a));
	Samurai::IO::Net::DNS::Message msg(&buf);
	return msg.decode() == Samurai::IO::Net::DNS::ResponseCode::Ok
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
	return rec->type_class.rr_type == Samurai::IO::Net::DNS::Type::A
		&& rec->type_class.rr_class == Samurai::IO::Net::DNS::Class::IN
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
		dynamic_cast<Samurai::IO::Net::DNS::RR_A*>(rec->rr.get());
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
	return msg.decode() == Samurai::IO::Net::DNS::ResponseCode::FormatError;
});

/* ------------------------------------------------------------------------- */
/* Name comparison                                                           */
/*                                                                           */
/* Name(const char*) only stores the text; split() is what produces the       */
/* labels that the comparison operators work on.                             */
/* ------------------------------------------------------------------------- */

EXO_TEST(dns_name_equal_to_itself,
{
	Samurai::IO::Net::DNS::Name a("www.example.com");
	a.split();
	Samurai::IO::Net::DNS::Name b("www.example.com");
	b.split();
	return a == b && !(a != b);
});

EXO_TEST(dns_name_case_insensitive,
{
	Samurai::IO::Net::DNS::Name a("WWW.Example.COM");
	a.split();
	Samurai::IO::Net::DNS::Name b("www.example.com");
	b.split();
	return a == b;
});

/*
 * These share their first two labels and differ only in the last. An
 * operator!= that returns as soon as it finds one *matching* label reports
 * them as equal.
 */
EXO_TEST(dns_name_differing_last_label,
{
	Samurai::IO::Net::DNS::Name a("www.example.com");
	a.split();
	Samurai::IO::Net::DNS::Name b("www.example.org");
	b.split();
	return (a != b) && !(a == b);
});

EXO_TEST(dns_name_differing_first_label,
{
	Samurai::IO::Net::DNS::Name a("www.example.com");
	a.split();
	Samurai::IO::Net::DNS::Name b("ftp.example.com");
	b.split();
	return (a != b) && !(a == b);
});

EXO_TEST(dns_name_differing_label_count,
{
	Samurai::IO::Net::DNS::Name a("example.com");
	a.split();
	Samurai::IO::Net::DNS::Name b("www.example.com");
	b.split();
	return (a != b) && !(a == b);
});

/* ------------------------------------------------------------------------- */
/* Record expiry                                                             */
/* ------------------------------------------------------------------------- */

/* An unstamped record has never been through a cache and does not expire. */
EXO_TEST(dns_record_unstamped_never_expires,
{
	Samurai::IO::Net::DNS::ResourceRecord rec;
	rec.ttl = 0;
	return !rec.isExpired() && rec.getExpiryTime() == 0;
});

EXO_TEST(dns_record_zero_ttl_expires_at_once,
{
	Samurai::IO::Net::DNS::ResourceRecord rec;
	rec.ttl = 0;
	rec.stampExpiry();
	return rec.isExpired();
});

EXO_TEST(dns_record_negative_ttl_expires_at_once,
{
	Samurai::IO::Net::DNS::ResourceRecord rec;
	rec.ttl = -1;
	rec.stampExpiry();
	return rec.isExpired();
});

EXO_TEST(dns_record_live_ttl_does_not_expire,
{
	Samurai::IO::Net::DNS::ResourceRecord rec;
	rec.ttl = 3600;
	rec.stampExpiry();
	return !rec.isExpired() && rec.getExpiryTime() != 0;
});

/* ------------------------------------------------------------------------- */
/* CacheStorage                                                              */
/*                                                                           */
/* The store is a singleton, so each case uses a name of its own rather than  */
/* assuming an empty cache.                                                  */
/* ------------------------------------------------------------------------- */

static std::unique_ptr<Samurai::IO::Net::DNS::ResourceRecord> dns_make_record(const char* host, int32_t ttl)
{
	auto rec = std::make_unique<Samurai::IO::Net::DNS::ResourceRecord>();
	Samurai::IO::Net::DNS::Name want(host);
	want.split();
	rec->name = want;
	rec->ttl = ttl;
	return rec;
}

EXO_TEST(dns_cache_lookup_finds_added_record,
{
	Samurai::IO::Net::DNS::CacheStorage* cache =
		Samurai::IO::Net::DNS::CacheStorage::getInstance();
	cache->add(dns_make_record("found.cachetest.invalid", 3600));

	Samurai::IO::Net::DNS::Name want("found.cachetest.invalid");
	want.split();
	return cache->lookup(want) != 0;
});

EXO_TEST(dns_cache_lookup_misses_unknown_name,
{
	Samurai::IO::Net::DNS::CacheStorage* cache =
		Samurai::IO::Net::DNS::CacheStorage::getInstance();

	Samurai::IO::Net::DNS::Name want("absent.cachetest.invalid");
	want.split();
	return cache->lookup(want) == 0;
});

/* A record whose time to live has run out must not be handed back. */
EXO_TEST(dns_cache_does_not_return_expired_record,
{
	Samurai::IO::Net::DNS::CacheStorage* cache =
		Samurai::IO::Net::DNS::CacheStorage::getInstance();
	cache->add(dns_make_record("stale.cachetest.invalid", 0));

	Samurai::IO::Net::DNS::Name want("stale.cachetest.invalid");
	want.split();
	return cache->lookup(want) == 0;
});

EXO_TEST(dns_cache_expire_drops_expired_records,
{
	Samurai::IO::Net::DNS::CacheStorage* cache =
		Samurai::IO::Net::DNS::CacheStorage::getInstance();
	const size_t before = cache->size();
	cache->add(dns_make_record("dropme.cachetest.invalid", 0));
	cache->expire();
	return cache->size() <= before;
});

EXO_TEST(dns_cache_keeps_live_record_across_expire,
{
	Samurai::IO::Net::DNS::CacheStorage* cache =
		Samurai::IO::Net::DNS::CacheStorage::getInstance();
	cache->add(dns_make_record("keepme.cachetest.invalid", 3600));
	cache->expire();

	Samurai::IO::Net::DNS::Name want("keepme.cachetest.invalid");
	want.split();
	return cache->lookup(want) != 0;
});

/* The store is bounded, so a flood of live records must not grow it without
 * limit. */
EXO_TEST(dns_cache_is_bounded,
{
	Samurai::IO::Net::DNS::CacheStorage* cache =
		Samurai::IO::Net::DNS::CacheStorage::getInstance();
	for (size_t n = 0; n < Samurai::IO::Net::DNS::DNS_CACHE_MAX_STORAGE * 2; n++)
		cache->add(dns_make_record("flood.cachetest.invalid", 3600));

	return cache->size() <= Samurai::IO::Net::DNS::DNS_CACHE_MAX_STORAGE;
});

EXO_TEST(dns_cache_add_null_is_harmless,
{
	Samurai::IO::Net::DNS::CacheStorage* cache =
		Samurai::IO::Net::DNS::CacheStorage::getInstance();
	const size_t before = cache->size();
	cache->add(nullptr);
	return cache->size() == before;
});

/* ------------------------------------------------------------------------- */
/* Name compression                                                          */
/*                                                                           */
/* This is the part of the decoder an attacker controls most directly: a      */
/* pointer says "the rest of this name is over there", and a decoder that     */
/* follows one carelessly can be made to loop or to read out of bounds.       */
/*                                                                           */
/* Samurai's defence is that a pointer may only target a label the message    */
/* has already decoded - compTbl - so a name can never point forward or at    */
/* itself, and following one always moves back over ground already covered.   */
/* The cases below pin that, and the fourteen bit pointer format.             */
/* ------------------------------------------------------------------------- */

namespace {

/* Builders rather than literals: a braced initialiser inside EXO_TEST would be
   split on its commas. */
struct Wire
{
	std::vector<unsigned char> bytes;

	void u8(unsigned v)  { bytes.push_back((unsigned char) v); }
	void u16(unsigned v) { u8(v >> 8); u8(v & 0xff); }

	void label(const std::string& text)
	{
		u8(text.size());
		bytes.insert(bytes.end(), text.begin(), text.end());
	}

	/* qdcount 1, plus however many answers the case needs. */
	void header(unsigned answers)
	{
		u16(0x1234);
		u16(0x8180);
		u16(1);
		u16(answers);
		u16(0);
		u16(0);
	}

	/* type A, class IN, ttl 60, four bytes of address. */
	void rdata_a()
	{
		u16(1); u16(1); u16(0); u16(60); u16(4);
		u8(10); u8(0); u8(0); u8(1);
	}

	void pointer(size_t target)
	{
		u8(0xc0 | ((target >> 8) & 0x3f));
		u8(target & 0xff);
	}

	size_t size() const { return bytes.size(); }
};

/* Decode and report the response code and the name of a given answer. */
static bool dns_decodes_with_name(Wire& wire, size_t record, const char* expect)
{
	Samurai::IO::Buffer buf;
	buf.append((const char*) wire.bytes.data(), wire.bytes.size());

	Samurai::IO::Net::DNS::Message msg(&buf);
	if (msg.decode() != Samurai::IO::Net::DNS::ResponseCode::Ok) return false;

	Samurai::IO::Net::DNS::ResourceRecord* rr = msg.getRecord(record);
	return rr && rr->name.toString() == expect;
}

static bool dns_decode_fails(Wire& wire)
{
	Samurai::IO::Buffer buf;
	buf.append((const char*) wire.bytes.data(), wire.bytes.size());

	Samurai::IO::Net::DNS::Message msg(&buf);
	return msg.decode() != Samurai::IO::Net::DNS::ResponseCode::Ok;
}

/* A question whose name is 12 labels of 20 characters, which pushes everything
   after it past offset 255 - the point of the exercise. */
static void dns_long_question(Wire& wire)
{
	for (int n = 0; n < 12; n++)
		wire.label(std::string(20, (char) ('a' + n)));
	wire.u8(0);
	wire.u16(1);
	wire.u16(1);
}

}

/* The ordinary case: an answer whose name points back at the question. */
EXO_TEST(dns_compression_pointer_is_followed,
{
	Wire wire;
	wire.header(1);
	wire.label("example"); wire.label("com"); wire.u8(0);
	wire.u16(1); wire.u16(1);

	wire.pointer(12);   /* the question name, immediately after the header */
	wire.rdata_a();

	return dns_decodes_with_name(wire, 0, "example.com.");
});

/*
 * A pointer carries fourteen bits, not eight. Treating it as the single byte
 * 0xc0 followed by an eight bit offset cannot express a target past 255, so a
 * perfectly ordinary response - one with enough records to push a name beyond
 * that - failed to decode at all.
 */
EXO_TEST(dns_compression_pointer_past_offset_255,
{
	Wire wire;
	wire.header(2);
	dns_long_question(wire);

	const size_t target = wire.size();
	wire.label("host"); wire.label("example"); wire.u8(0);
	wire.rdata_a();

	wire.pointer(target);
	wire.rdata_a();

	/* The fixture is only meaningful if the target really is out of byte range. */
	if (target <= 255) return false;

	return dns_decodes_with_name(wire, 1, "host.example.");
});

EXO_TEST(dns_compression_far_pointer_matches_the_uncompressed_name,
{
	Wire wire;
	wire.header(2);
	dns_long_question(wire);

	const size_t target = wire.size();
	wire.label("host"); wire.label("example"); wire.u8(0);
	wire.rdata_a();

	wire.pointer(target);
	wire.rdata_a();

	Samurai::IO::Buffer buf;
	buf.append((const char*) wire.bytes.data(), wire.bytes.size());
	Samurai::IO::Net::DNS::Message msg(&buf);
	if (msg.decode() != Samurai::IO::Net::DNS::ResponseCode::Ok) return false;

	Samurai::IO::Net::DNS::ResourceRecord* a = msg.getRecord((size_t) 0);
	Samurai::IO::Net::DNS::ResourceRecord* b = msg.getRecord((size_t) 1);
	return a && b && a->name == b->name;
});

/* ------------------------------------------------------------------------- */
/* Pointers that must be refused                                             */
/* ------------------------------------------------------------------------- */

/* Nothing was decoded at offset 200, so the pointer has no legitimate target. */
EXO_TEST(dns_compression_pointer_to_unseen_offset_is_refused,
{
	Wire wire;
	wire.header(1);
	wire.label("example"); wire.label("com"); wire.u8(0);
	wire.u16(1); wire.u16(1);

	wire.pointer(200);
	wire.rdata_a();

	return dns_decode_fails(wire);
});

/* A pointer at its own offset would loop forever if it were followed. */
EXO_TEST(dns_compression_self_referential_pointer_terminates,
{
	Wire wire;
	wire.header(1);
	wire.label("example"); wire.label("com"); wire.u8(0);
	wire.u16(1); wire.u16(1);

	const size_t here = wire.size();
	wire.pointer(here);
	wire.rdata_a();

	return dns_decode_fails(wire);
});

/* Two pointers at each other: the classic decompression loop. */
EXO_TEST(dns_compression_mutual_pointers_terminate,
{
	Wire wire;
	wire.header(1);
	wire.label("example"); wire.label("com"); wire.u8(0);
	wire.u16(1); wire.u16(1);

	const size_t first = wire.size();
	wire.pointer(first + 2);
	wire.pointer(first);
	wire.rdata_a();

	return dns_decode_fails(wire);
});

/* A pointer past the end of the message must be refused, not read. */
EXO_TEST(dns_compression_pointer_past_the_buffer_is_refused,
{
	Wire wire;
	wire.header(1);
	wire.label("example"); wire.label("com"); wire.u8(0);
	wire.u16(1); wire.u16(1);

	wire.pointer(0x3fff);
	wire.rdata_a();

	return dns_decode_fails(wire);
});

/*
 * A trailing byte that begins a pointer but has nothing after it. decode()
 * stops reading records once fewer than twelve bytes remain - the smallest a
 * record header can be - so the stub never reaches decodeName. The message is
 * accepted with the tail ignored, which is the tolerant reading; what matters
 * is that nothing is read past the end and no half-built record appears.
 */
EXO_TEST(dns_compression_truncated_pointer_yields_no_record,
{
	Wire wire;
	wire.header(1);
	wire.label("example"); wire.label("com"); wire.u8(0);
	wire.u16(1); wire.u16(1);

	wire.u8(0xc0);   /* and nothing after it */

	Samurai::IO::Buffer buf;
	buf.append((const char*) wire.bytes.data(), wire.bytes.size());

	Samurai::IO::Net::DNS::Message msg(&buf);
	msg.decode();
	return msg.getRecord((size_t) 0) == nullptr;
});

/* A pointer forward into a name not yet decoded is refused for the same reason
   a self-reference is: only what is already behind us is a legal target. */
EXO_TEST(dns_compression_forward_pointer_is_refused,
{
	Wire wire;
	wire.header(1);
	wire.label("example"); wire.label("com"); wire.u8(0);
	wire.u16(1); wire.u16(1);

	const size_t here = wire.size();
	wire.pointer(here + 8);
	wire.rdata_a();

	return dns_decode_fails(wire);
});

/* ------------------------------------------------------------------------- */
/* ResolveConfiguration                                                      */
/*                                                                           */
/* The constructor takes the path, defaulting to /etc/resolv.conf, so the     */
/* parser can be pointed at a fixture instead of at the host's real one -     */
/* which would make the result depend on the machine.                        */
/* ------------------------------------------------------------------------- */

namespace {

/* Writes a resolv.conf, parses it, and removes it. */
class ResolvFixture
{
	public:
		explicit ResolvFixture(const char* body)
		{
			static int counter = 0;
			path = "samurai-resolv-test-" + std::to_string(counter++);

			Samurai::IO::File f(path);
			if (f.open(Samurai::IO::File::Mode::Write | Samurai::IO::File::Mode::Truncate))
			{
				f.write(body, strlen(body));
				f.close();
				ok = true;
			}
		}

		~ResolvFixture() { Samurai::IO::File::remove(path.c_str()); }

		const char* name() const { return path.c_str(); }
		bool valid() const { return ok; }

		ResolvFixture(const ResolvFixture&) = delete;
		ResolvFixture& operator=(const ResolvFixture&) = delete;

	private:
		std::string path;
		bool ok = false;
};

}

EXO_TEST(resolvconf_reads_a_nameserver,
{
	ResolvFixture fixture("nameserver 192.0.2.1\n");
	if (!fixture.valid()) return false;

	Samurai::IO::Net::DNS::ResolveConfiguration config(fixture.name());
	const Samurai::IO::Net::InetAddress* server = config.getNameServer(0);

	return config.getNameServerCount() == 1
		&& server && server->toString() == "192.0.2.1";
});

EXO_TEST(resolvconf_reads_several_nameservers,
{
	ResolvFixture fixture("nameserver 192.0.2.1\nnameserver 192.0.2.2\nnameserver 192.0.2.3\n");
	if (!fixture.valid()) return false;

	Samurai::IO::Net::DNS::ResolveConfiguration config(fixture.name());
	return config.getNameServerCount() == 3;
});

/* Tabs and runs of spaces after the keyword are as legal as one space. */
EXO_TEST(resolvconf_tolerates_whitespace,
{
	ResolvFixture fixture("nameserver\t\t 192.0.2.9\n");
	if (!fixture.valid()) return false;

	Samurai::IO::Net::DNS::ResolveConfiguration config(fixture.name());
	const Samurai::IO::Net::InetAddress* server = config.getNameServer(0);
	return server && server->toString() == "192.0.2.9";
});

EXO_TEST(resolvconf_ignores_comments_and_blank_lines,
{
	ResolvFixture fixture("# a comment\n\n; another\nnameserver 192.0.2.4\n\n");
	if (!fixture.valid()) return false;

	Samurai::IO::Net::DNS::ResolveConfiguration config(fixture.name());
	return config.getNameServerCount() == 1;
});

/* A line that is not an address must not become a name server. */
EXO_TEST(resolvconf_rejects_a_malformed_address,
{
	ResolvFixture fixture("nameserver not-an-address\nnameserver 192.0.2.5\n");
	if (!fixture.valid()) return false;

	Samurai::IO::Net::DNS::ResolveConfiguration config(fixture.name());
	return config.getNameServerCount() == 1;
});

EXO_TEST(resolvconf_accepts_an_ipv6_nameserver,
{
	ResolvFixture fixture("nameserver 2001:db8::1\n");
	if (!fixture.valid()) return false;

	Samurai::IO::Net::DNS::ResolveConfiguration config(fixture.name());
	const Samurai::IO::Net::InetAddress* server = config.getNameServer(0);
	return config.getNameServerCount() == 1 && server && server->isValid();
});

/* ------------------------------------------------------------------------- */
/* options                                                                   */
/* ------------------------------------------------------------------------- */

EXO_TEST(resolvconf_option_defaults,
{
	ResolvFixture fixture("nameserver 192.0.2.1\n");
	if (!fixture.valid()) return false;

	Samurai::IO::Net::DNS::ResolveConfiguration config(fixture.name());
	return config.getTimeout() == 5
		&& config.getAttempts() == 2
		&& config.getNDots() == 1
		&& !config.isRotate() && !config.isIPv6() && !config.isDebug();
});

EXO_TEST(resolvconf_option_flags,
{
	ResolvFixture fixture("nameserver 192.0.2.1\noptions rotate inet6 debug\n");
	if (!fixture.valid()) return false;

	Samurai::IO::Net::DNS::ResolveConfiguration config(fixture.name());
	return config.isRotate() && config.isIPv6() && config.isDebug();
});

EXO_TEST(resolvconf_option_numbers,
{
	ResolvFixture fixture("nameserver 192.0.2.1\noptions timeout:3 attempts:5 ndots:4\n");
	if (!fixture.valid()) return false;

	Samurai::IO::Net::DNS::ResolveConfiguration config(fixture.name());
	return config.getTimeout() == 3
		&& config.getAttempts() == 5
		&& config.getNDots() == 4;
});

/* A zero or negative value is refused rather than accepted as a timeout. */
EXO_TEST(resolvconf_option_zero_is_refused,
{
	ResolvFixture fixture("nameserver 192.0.2.1\noptions timeout:0 attempts:0\n");
	if (!fixture.valid()) return false;

	Samurai::IO::Net::DNS::ResolveConfiguration config(fixture.name());
	return config.getTimeout() == 5 && config.getAttempts() == 2;
});

/* ------------------------------------------------------------------------- */
/* Nothing to resolve with                                                   */
/* ------------------------------------------------------------------------- */

/* The header says callers must check for null; a file with no usable server is
   exactly the case that produces it. */
EXO_TEST(resolvconf_no_nameserver_yields_null,
{
	ResolvFixture fixture("# nothing useful here\noptions rotate\n");
	if (!fixture.valid()) return false;

	Samurai::IO::Net::DNS::ResolveConfiguration config(fixture.name());
	return config.getNameServerCount() == 0 && config.getNameServer(0) == nullptr;
});

EXO_TEST(resolvconf_missing_file_yields_null,
{
	Samurai::IO::Net::DNS::ResolveConfiguration config("samurai-resolv-does-not-exist");
	return config.getNameServerCount() == 0 && config.getNameServer(0) == nullptr;
});

EXO_TEST(resolvconf_empty_file_yields_null,
{
	ResolvFixture fixture("");
	if (!fixture.valid()) return false;

	Samurai::IO::Net::DNS::ResolveConfiguration config(fixture.name());
	return config.getNameServer(0) == nullptr;
});

/* getNameServer() takes the attempt number, and must stay in range however
   many times it is asked. */
EXO_TEST(resolvconf_nameserver_selection_stays_in_range,
{
	ResolvFixture fixture("nameserver 192.0.2.1\nnameserver 192.0.2.2\n");
	if (!fixture.valid()) return false;

	Samurai::IO::Net::DNS::ResolveConfiguration config(fixture.name());
	for (size_t attempt = 0; attempt < 16; attempt++)
	{
		const Samurai::IO::Net::InetAddress* server = config.getNameServer(attempt);
		if (!server || !server->isValid()) return false;
	}
	return true;
});
