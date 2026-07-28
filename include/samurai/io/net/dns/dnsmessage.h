/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SYSTEM_DNS_MESSAGE_H
#define HAVE_SYSTEM_DNS_MESSAGE_H

#include <samurai/samurai.h>
#include <samurai/io/net/dns/common.h>
#include <samurai/io/net/dns/dnsutil.h>
#include <samurai/io/net/dns/dnsrrs.h>

namespace Samurai {
namespace IO {

class Buffer;

namespace Net {

class DatagramPacket;

namespace DNS {

/**
 * This represents a DNS message header, either a request or response.
 */
class MessageHeader {
	public:
		uint16_t id = 0;
		/*
		 * The flag word is held as a plain 16-bit value and picked apart with
		 * the masks below. decode16Bits() has already put it in host order, so
		 * the masks mean the same thing on either endianness - which a
		 * bitfield layered over the same storage would not have.
		 */
		uint16_t flags_u16 = 0;
		uint16_t qdcount = 0; /* number of questions in query */
		uint16_t ancount = 0; /* number of resource records in answer */
		uint16_t nscount = 0; /* number of name servers */
		uint16_t arcount = 0; /* number of additional records */
		
	public:

	
		uint16_t getID() const {
			return id;
		}

		bool     isQuery() {
			return (flags_u16 & 0x8000) == 0;
		}

		bool     isResponse() {
			return (flags_u16 & 0x8000) != 0;
		}

		QueryType getQueryType() {
			switch ((flags_u16 & 0x7800) >> 11) {
				case 0: return QueryType::Query;
				case 1: return QueryType::InverseQuery;
				case 2: return QueryType::Status;
				default: return QueryType::Reserved;
			}
		}

		const char* getQueryTypeStr()
		{
			switch ((flags_u16 & 0x7800) >> 11) {
				case 0: return "standard";
				case 1: return "inverse";
				case 2: return "status";
				default: return "reserved";
			}
		}

		bool     isAuthorative()
		{
			return (flags_u16 & 0x0400);
		}

		bool     isTruncated() {
			return (flags_u16 & 0x0200);
		}

		bool     isRecursionDesired() {
			return (flags_u16 & 0x0100);
		}

		bool     isRecursionAvailable() {
			return (flags_u16 & 0x0080);
		}

		bool     isValid() {
			/* The opcode has to be masked off before it is shifted down: '>>'
			 * binds tighter than '&', so folding the two into one expression
			 * tests the response code instead and rejects the perfectly valid
			 * codes 3, 4 and 5 (name error, not implemented, refused). */
			return (
				((flags_u16 & 0x000f) <= 5) &&
				(((flags_u16 & 0x7800) >> 11) <= 2) &&
				((flags_u16 & 0x0070) == 0)
				);
		}
		
		ResponseCode getResponseCode()
		{
			switch (flags_u16 & 0x000f) {
				case 0: return ResponseCode::Ok;
				case 1: return ResponseCode::FormatError;
				case 2: return ResponseCode::ServerError;
				case 3: return ResponseCode::NameError;
				case 4: return ResponseCode::NotImplemented;
				case 5: return ResponseCode::Refused;
				default:
					return ResponseCode::Reserved;
			}
		}

		const char* getResponseCodeStr()
		{
			switch (flags_u16 & 0x000f) {
				case 0: return "ok";
				case 1: return "format error";
				case 2: return "server error";
				case 3: return "name error";
				case 4: return "query not implemented";
				case 5: return "query refused";
				default:
					return "unknown/reserved/invalid error";
			}
		}
		
	public:
		void dump();
};





class Question {
	public:
		Name name;
		TypeClass type_class;
};



/**
 * This represents a DNS message, and is used encode and decode DNS messages.
 */
class Message {
	public:
		Message();
		Message(Samurai::IO::Buffer* buffer);
		~Message();

		Samurai::IO::Net::DNS::ResponseCode decode();
		bool encode();
	
		bool isResponse();

		/* The returned records stay owned by this message. */
		ResourceRecord* getRecord(Name* name);
		ResourceRecord* getRecord(size_t index);

		/**
		 * Hand the decoded answer records over to the caller, which becomes
		 * responsible for destroying them. The message keeps none of them, so
		 * getRecord() finds nothing afterwards.
		 */
		std::vector<ResourceRecord*> releaseRecords();

		/* Owns its decoded records, so a copy would release them twice. */
		Message(const Message&) = delete;
		Message& operator=(const Message&) = delete;

	private:
		void addOffset(size_t offset);
		bool isOffsetOK(size_t offset);

		bool decodeName(size_t& offset, Name& name, size_t recursion = 0, size_t maxlen = 0);
		bool decode16Bits(size_t& offset, uint16_t& data);

		/**
		 * Read a 16 bit field into one of the scoped wire enums.
		 *
		 * The single place an arbitrary value off the network becomes a Type or
		 * a Class: neither enumerates every value a peer may send, so the cast
		 * is deliberate and belongs here rather than at each call site.
		 */
		template<typename E>
		bool decodeEnum16(size_t& offset, E& value)
		{
			uint16_t raw = 0;
			if (!decode16Bits(offset, raw)) return false;
			value = static_cast<E>(raw);
			return true;
		}
		bool decode32Bits(size_t& offset, uint32_t& data);
		bool decodeS32Bits(size_t& offset, int32_t& data);
		
	
	protected:
		MessageHeader header;
		std::vector<Question*> questions;
		std::vector<ResourceRecord*> records;
		std::vector<ResourceRecord*> nameservers;
		std::vector<ResourceRecord*> additional;

	private:
		std::vector<uint8_t> compTbl;
		Samurai::IO::Buffer* buffer;
};



} // namespace DNS
} // namespace Net
} // namespace IO
} // namespace Samurai

#endif // HAVE_SYSTEM_DNS_MESSAGE_H
