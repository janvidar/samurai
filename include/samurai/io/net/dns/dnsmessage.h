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
		uint16_t id;
		union
		{
			struct
			{
				uint16_t qr     : 1; /* query(0) or response(1) */
				uint16_t opcode : 4; /* query type */
				uint16_t aa     : 1; /* authorative answer */
				uint16_t tc     : 1; /* was the message truncated? */
				uint16_t rd     : 1; /* recursion desired */
				uint16_t ra     : 1; /* recursion available */
				uint16_t z      : 3; /* must be zero */
				uint16_t rcode  : 4; /* response code */
			} flags;
			uint16_t flags_u16;
		};
		uint16_t qdcount; /* number of questions in query */
		uint16_t ancount; /* number of resource records in answer */
		uint16_t nscount; /* number of name servers */
		uint16_t arcount; /* number of additional records */
		
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

		enum QueryType getQueryType() {
			switch ((flags_u16 & 0x7800) >> 11) {
				case 0: return DNS_QT_QUERY;
				case 1: return DNS_QT_IQUERY;
				case 2: return DNS_QT_STATUS;
				default: return DNS_QT_RESERVED;
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
			return (
				((flags_u16 & 0x000f) <= 5) &&
				((flags_u16 & 0x7800 >> 11) <= 2) &&
				((flags_u16 & 0x0070) == 0)
				);
		}
		
		enum ResponseCode getResponseCode()
		{
			switch (flags_u16 & 0x000f) {
				case 0: return DNS_STATUS_OK;
				case 1: return DNS_STATUS_FORMAT_ERROR;
				case 2: return DNS_STATUS_SERVER_ERROR;
				case 3: return DNS_STATUS_NAME_ERROR;
				case 4: return DNS_STATUS_NOT_IMPLEMENTED;
				case 5: return DNS_STATUS_REFUSED;
				default:
					return DNS_STATUS_RESERVED;
			}
		}

		const char* getResponseCodeStr()
		{
			switch (flags_u16 & 0x000f) { // FIXME: This works on little endian.
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
		virtual ~Message();

		enum Samurai::IO::Net::DNS::ResponseCode decode();
		bool encode();
	
		bool isResponse();
		
		ResourceRecord* getRecord(Name* name);
		ResourceRecord* getRecord(size_t index);
	
	private:
		void addOffset(size_t offset);
		bool isOffsetOK(size_t offset);

		bool decodeName(size_t& offset, Name& name, size_t recursion = 0, size_t maxlen = 0);
		bool decode16Bits(size_t& offset, uint16_t& data);
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
