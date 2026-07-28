/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/net/dns/dnsmessage.h>
#include <samurai/io/net/datagram.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/buffer.h>
#include <memory>
#include <vector>

Samurai::IO::Net::DNS::Message::Message() {
	buffer = nullptr;
}

Samurai::IO::Net::DNS::Message::Message(Samurai::IO::Buffer* buffer_) {
	buffer = buffer_; // FIXME: perhaps make a copy?
}

/*
 * The buffer is borrowed from whoever handed it to the constructor, but every
 * record, question and name server decoded out of it belongs to this message.
 */
Samurai::IO::Net::DNS::Message::~Message() {
	for (Question* question : questions)
		delete question;

	for (ResourceRecord* record : records)
		delete record;

	for (ResourceRecord* record : nameservers)
		delete record;

	for (ResourceRecord* record : additional)
		delete record;
}

std::vector<Samurai::IO::Net::DNS::ResourceRecord*> Samurai::IO::Net::DNS::Message::releaseRecords()
{
	std::vector<ResourceRecord*> released;
	released.swap(records);
	return released;
}

bool Samurai::IO::Net::DNS::Message::isResponse()
{
	return header.isResponse();
}


bool Samurai::IO::Net::DNS::Message::isOffsetOK(size_t offset)
{
	if (offset > DNS_NAME_SIZE) return false;
	uint8_t t = (uint8_t) offset;
	for (std::vector<uint8_t>::iterator it = compTbl.begin(); it != compTbl.end(); it++)
		if (t == (*it)) return true;
	return false;
}

void Samurai::IO::Net::DNS::Message::addOffset(size_t offset) {
	if (isOffsetOK(offset)) return;
	compTbl.push_back((uint8_t) offset);
}


/*
 * DNS puts these on the wire in network order. Buffer::popBinary() does the
 * byte order conversion and the bounds check, and reads through memcpy() from
 * an unsigned byte.
 *
 * Assembling the value here from Buffer::at() instead would not work: at()
 * returns a (signed) char, so any byte with the high bit set becomes negative
 * and sign-extends across the bytes already shifted into place - "\x00\xff"
 * decoded as 0xffff rather than 0x00ff.
 */

bool Samurai::IO::Net::DNS::Message::decode16Bits(size_t& offset, uint16_t& data) {
	if (!buffer->popBinary(offset, data, Samurai::IO::Buffer::BigEndian)) return false;
	offset += sizeof(data);
	return true;
}

bool Samurai::IO::Net::DNS::Message::decode32Bits(size_t& offset, uint32_t& data) {
	if (!buffer->popBinary(offset, data, Samurai::IO::Buffer::BigEndian)) return false;
	offset += sizeof(data);
	return true;
}

bool Samurai::IO::Net::DNS::Message::decodeS32Bits(size_t& offset, int32_t& data_) {
	uint32_t data = 0;
	if (!buffer->popBinary(offset, data, Samurai::IO::Buffer::BigEndian)) return false;

	/* A TTL is a 31 bit value; the top bit is reserved and treated as zero. */
	data_ = (int32_t) (data & 0x7fffffff);
	offset += sizeof(data);
	return true;
}


bool Samurai::IO::Net::DNS::Message::decodeName(size_t& offset, Name& name, size_t recursion, size_t maxlen)
{
	size_t offset_start = offset;
	QDBG("Decoding name section at offset %d, recursion=%d, maxlen=%d", offset, recursion, maxlen);

	if (recursion > 64) {
		QDBG("decodeName: too many recursions");
		return false;
	}
	
	if (recursion)
	{
		QDBG("Recursion: %d, offset=%d", recursion, offset);
	}
	
	
	bool more = true;
	while (more) {
		if (offset >= buffer->size()) {
			QDBG("decodeName: Offset >= buffer->size(): 1");
			return false;
		}

		uint8_t section = buffer->at(offset);
		offset++;

		if (offset >= buffer->size()) {
			QDBG("decodeName: Offset >= buffer->size(): 2");
			return false;
		}

		if (section == 0 || (offset >= maxlen && maxlen > 0)) {
			QDBG("decodeName: Reaching max length of name");
			if (recursion) {
				return true;
			}
			more = false;
			continue;
		}

		if (section == 0xc0) {
			uint8_t ref = buffer->at(offset++);
			if (!isOffsetOK((size_t) ref)) {
				QDBG("decodeName: Offset lookup is invalid: %d\n", (int) ref);
				return false;
			}
			size_t old_offset = (size_t) ref;
			
			/*
			if (recursion && ref < offset) {
				offset--; offset--;
				more = false;
				continue;
			}
			*/
			
			if (!decodeName(old_offset, name, ++recursion, maxlen ? (offset_start+maxlen)-offset : 0)) {
				QDBG("decodeName: Recursive name decode failed\n");
				return false;
			}
			if (offset >= maxlen) {
				more = false;
				continue;
			}
			if ((uint8_t) buffer->at(offset) != (uint8_t) 0xc0)
				more = false;
			continue;
		}

		uint8_t len = section;
		if (buffer->size() < offset + len || len > DNS_LABEL_SIZE) return false;
		std::vector<char> tmp(len);
		buffer->pop(tmp.data(), offset, (size_t) len);
		Label label(tmp.data(), len);
		addOffset(offset-1);
		offset += (len);
		name.addPart(label);
		if (!label.isValid()) {
			QDBG("decodeName: Label is not valid\n");
			return false;
		}
	}
	if (!recursion && !name.countParts()) {
		QDBG("decodeName: name parts is zero\n");
		return false;
	}
	
	return true;
}


enum Samurai::IO::Net::DNS::ResponseCode Samurai::IO::Net::DNS::Message::decode()
{
	if (!buffer) return DNS_STATUS_FORMAT_ERROR;
	
	size_t offset  = 0;
	if (!decode16Bits(offset, header.id) ||
			!decode16Bits(offset, header.flags_u16) ||
			!decode16Bits(offset, header.qdcount) ||
			!decode16Bits(offset, header.ancount) ||
			!decode16Bits(offset, header.nscount) ||
			!decode16Bits(offset, header.arcount))
		return DNS_STATUS_FORMAT_ERROR;

	QDBG("DNS Response: id=%d, flags=%x, qd=%d, an=%d, ns=%d, ar=%d", header.id, header.flags_u16, header.qdcount, header.ancount, header.nscount, header.arcount);
	QDBG("* flags: { message type=%s, query type=%s, authorative=%s, truncated=%s, recursion desired=%s, recursion available=%s, response_code=%s }",
		(header.isQuery() ? "query" : "response"),
		 header.getQueryTypeStr(),
		(header.isAuthorative()        ? "yes" : "no"),
		(header.isTruncated()          ? "yes" : "no"),
		(header.isRecursionDesired()   ? "yes" : "no"),
		(header.isRecursionAvailable() ? "yes" : "no"),
		header.getResponseCodeStr());

	if (!header.isValid()) return DNS_STATUS_FORMAT_ERROR;
	
	for (int q = 0; q < (int) header.qdcount; q++) {
		Question question;

		if (!decodeName(offset, question.name, 0, 0) ||
			!decode16Bits(offset, question.type_class.rr_type) ||
			!decode16Bits(offset, question.type_class.rr_class))
		 return DNS_STATUS_FORMAT_ERROR;

		QDBG("name='%s', type=%d, class=%d", question.name.toString().c_str(), (int) question.type_class.rr_type, (int) question.type_class.rr_class);
	}
	
	int rrcount = header.ancount;
	/* if (!header.isTruncated())*/ rrcount += header.nscount + header.arcount;

	for (int q = 0; q < rrcount; q++) {
		if (buffer->size() - offset < 12) {
			QDBG("Truncated record");
			break;
		}

		/*
		 * Held by unique_ptr because the decode below gives up through a dozen
		 * different paths, and the record is only handed to 'records' once it
		 * has been decoded in full.
		 */
		std::unique_ptr<ResourceRecord> record(new ResourceRecord());

		if (!decodeName(offset, record->name, 0, 0) ||
			!decode16Bits(offset, record->type_class.rr_type) ||
			!decode16Bits(offset, record->type_class.rr_class) ||
			!decodeS32Bits(offset, record->ttl) ||
			!decode16Bits(offset, record->rdLength))
		{
			QDBG("Malformed resource record header");
			return DNS_STATUS_FORMAT_ERROR;
		}

		if (buffer->size() - offset < record->rdLength) {
			QDBG("Truncated sub record");
			break;
		}

		size_t maxRdOffset = (size_t) offset + record->rdLength;
		if (maxRdOffset > buffer->size()) { QDBG("Record data runs past the message"); return DNS_STATUS_FORMAT_ERROR; }

		if (record->type_class.rr_type == Type_CNAME) {
			Name name;
			if (!decodeName(offset, name, 0, maxRdOffset)) { QDBG("Malformed CNAME"); return DNS_STATUS_FORMAT_ERROR; }
			record->rr = std::make_unique<RR_CNAME>(name);

		} else if (record->type_class.rr_type == Type_PTR) {
			Name name;
			if (!decodeName(offset, name, 0, maxRdOffset)) { QDBG("Malformed PTR"); return DNS_STATUS_FORMAT_ERROR; }
			record->rr = std::make_unique<RR_PTR>(name);

		} else if (record->type_class.rr_type == Type_NS) {
			Name name;
			if (!decodeName(offset, name, 0, maxRdOffset)) { QDBG("Malformed NS"); return DNS_STATUS_FORMAT_ERROR; }
			record->rr = std::make_unique<RR_NS>(name);

		} else if (record->type_class.rr_type == Type_A) {
			/* An A record is exactly four bytes. A shorter one would leave the
			 * remaining bytes of the address undefined. */
			if (record->rdLength != 4) { QDBG("A record is not 4 bytes"); return DNS_STATUS_FORMAT_ERROR; }
			char addr_bytes[4];
			if (buffer->pop(addr_bytes, offset, sizeof(addr_bytes)) != sizeof(addr_bytes))
				{ QDBG("Truncated A record"); return DNS_STATUS_FORMAT_ERROR; }
			offset += record->rdLength;
			Samurai::IO::Net::InetAddress inet_addr;
			inet_addr.setRawAddress(addr_bytes, sizeof(addr_bytes), Samurai::IO::Net::InetAddress::IPv4);
			record->rr = std::make_unique<RR_A>(inet_addr);

		} else if (record->type_class.rr_type == Type_AAAA) {
			if (record->rdLength != 16) { QDBG("AAAA record is not 16 bytes"); return DNS_STATUS_FORMAT_ERROR; }
			char addr_bytes[16];
			if (buffer->pop(addr_bytes, offset, sizeof(addr_bytes)) != sizeof(addr_bytes))
				{ QDBG("Truncated AAAA record"); return DNS_STATUS_FORMAT_ERROR; }
			offset += record->rdLength;
			Samurai::IO::Net::InetAddress inet_addr;
			inet_addr.setRawAddress(addr_bytes, sizeof(addr_bytes), Samurai::IO::Net::InetAddress::IPv6);
			record->rr = std::make_unique<RR_AAAA>(inet_addr);

		} else if (record->type_class.rr_type == Type_SOA) {
			Name primary;
			Name email;
			uint32_t serial = 0;
			uint32_t refresh = 0;
			uint32_t retry = 0;
			uint32_t expire = 0;
			int32_t ttl = 0;
			if (!decodeName(offset, primary, 0, 0) ||
					!decodeName(offset, email, 0, 0) ||
					!decode32Bits(offset, serial) ||
					!decode32Bits(offset, refresh) ||
					!decode32Bits(offset, retry) ||
					!decode32Bits(offset, expire) ||
					!decodeS32Bits(offset, ttl))
				{ QDBG("Malformed SOA"); return DNS_STATUS_FORMAT_ERROR; }

			record->rr = std::make_unique<RR_SOA>(primary, email, serial, refresh, retry, expire, ttl);

		} else {
			// Ignore unknown data type
			QDBG("Unknown RR type: %d", (int) record->type_class.rr_type);
			offset += record->rdLength;
		}

		records.push_back(record.release());
	}

	return DNS_STATUS_OK;
}


Samurai::IO::Net::DNS::ResourceRecord* Samurai::IO::Net::DNS::Message::getRecord(Samurai::IO::Net::DNS::Name* name)
{
	QDBG("getRecord(%p) = '%s'\n", name, name ? name->toString().c_str() : "");
	
	if (!name) return nullptr;
	
	QDBG("Records: %d\n", (int) records.size());
	for (std::vector<Samurai::IO::Net::DNS::ResourceRecord*>::iterator it = records.begin(); it != records.end(); it++) {
		Samurai::IO::Net::DNS::ResourceRecord* record = (*it);
		QDBG("Record: '%s' == '%s', %d\n", record->name.toString().c_str(), name->toString().c_str(), (int) record->type_class.rr_type);
		if (record->name == *name /*&& record->type_class.rr_type == (uint16_t) Type_A*/)
		{
			QDBG("Match!\n");
			return record;
		}
	}
	return nullptr;
}

Samurai::IO::Net::DNS::ResourceRecord* Samurai::IO::Net::DNS::Message::getRecord(size_t index)
{
	if (index >= records.size() || !records.size()) return nullptr;
	return records[index];
}




