/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/net/dns/dnsmessage.h>
#include <samurai/io/net/datagram.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/buffer.h>

#define DATADUMP

Samurai::IO::Net::DNS::Message::Message() {
	buffer = 0;
}

Samurai::IO::Net::DNS::Message::Message(Samurai::IO::Buffer* buffer_) {
	buffer = buffer_; // FIXME: perhaps make a copy?
}

Samurai::IO::Net::DNS::Message::~Message() {
	// free buffer?
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


bool Samurai::IO::Net::DNS::Message::decode16Bits(size_t& offset, uint16_t& data) {
	if (offset+2 > buffer->size()) return false;
	data = ((buffer->at(offset) << 8) | (buffer->at(offset+1)));
	offset += 2;
	return true;
}

bool Samurai::IO::Net::DNS::Message::decode32Bits(size_t& offset, uint32_t& data) {
	if (offset+4 > buffer->size()) return false;
	data = ((buffer->at(offset) << 24) | (buffer->at(offset+1) << 16) | (buffer->at(offset+2) << 8) | (buffer->at(offset+3)));
	offset += 4;
	return true;
}

bool Samurai::IO::Net::DNS::Message::decodeS32Bits(size_t& offset, int32_t& data_) {
	if (offset+4 > buffer->size()) return false;
	
	uint32_t data = ((buffer->at(offset) << 24) | (buffer->at(offset+1) << 16) | (buffer->at(offset+2) << 8) | (buffer->at(offset+3)));
	data_ = (int32_t) (data & 0x1fffffff);
	offset += 4;
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
		char* tmp = new char[len];
		buffer->pop(tmp, offset, (size_t) len);
		Label* label = new Label(tmp, len);
		addOffset(offset-1);
		offset += (len);
		name.addPart(label);
		delete[] tmp;
		if (!label->isValid()) {
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

#ifdef DATADUMP
	printf("DNS Response: id=%d, flags=%x, qd=%d, an=%d, ns=%d, ar=%d\n", header.id, header.flags_u16, header.qdcount, header.ancount, header.nscount, header.arcount);
	
	printf("* flags: { message type=%s, query type=%s, authorative=%s, truncated=%s, recursion desired=%s, recursion available=%s, response_code=%s }\n", 
		(header.isQuery() ? "query" : "response"),
		 header.getQueryTypeStr(),
		(header.isAuthorative()        ? "yes" : "no"),
		(header.isTruncated()          ? "yes" : "no"),
		(header.isRecursionDesired()   ? "yes" : "no"),
		(header.isRecursionAvailable() ? "yes" : "no"),
		header.getResponseCodeStr());
#endif

	if (!header.isValid()) return DNS_STATUS_FORMAT_ERROR;
	
	for (int q = 0; q < (int) header.qdcount; q++) {
		Question question;

		if (!decodeName(offset, question.name, 0, 0) ||
			!decode16Bits(offset, question.type_class.rr_type) ||
			!decode16Bits(offset, question.type_class.rr_class))
		 return DNS_STATUS_FORMAT_ERROR;

#ifdef DATADUMP
		printf("name='%s', type=%d, class=%d\n", question.name.toString(), (int) (uint16_t) question.type_class.rr_type, (int) (uint16_t) question.type_class.rr_class);
#endif
	}
	
	int rrcount = header.ancount;
	/* if (!header.isTruncated())*/ rrcount += header.nscount + header.arcount;

	for (int q = 0; q < rrcount; q++) {
		if (buffer->size() - offset < 12) {
			printf("Truncated record\n");
			break;
		}
	
		ResourceRecord* record = new ResourceRecord();
		
		if (!decodeName(offset, *record->name, 0, 0) ||
			!decode16Bits(offset, record->type_class.rr_type) ||
			!decode16Bits(offset, record->type_class.rr_class) ||
			!decodeS32Bits(offset, record->ttl) ||
			!decode16Bits(offset, record->rdLength))
		{
			delete record;
			printf("WTF 1?\n");
			return DNS_STATUS_FORMAT_ERROR;
		}

		if (buffer->size() - offset < record->rdLength) {
			printf("Truncated sub record\n");
			delete record;
			break;
		}

		size_t maxRdOffset = (size_t) offset + record->rdLength;
		if (maxRdOffset > buffer->size()) { printf("WTF 2?\n"); return DNS_STATUS_FORMAT_ERROR; }

		if (record->type_class.rr_type == Type_CNAME) {
			Name name;
			if (!decodeName(offset, name, 0, maxRdOffset)) { printf("WTF 3?\n"); return DNS_STATUS_FORMAT_ERROR; }
			record->rr = new RR_CNAME(name);
		
		} else if (record->type_class.rr_type == Type_PTR) {
			Name name;
			if (!decodeName(offset, name, 0, maxRdOffset)) { printf("WTF 4?\n"); return DNS_STATUS_FORMAT_ERROR; }
			record->rr = new RR_PTR(name);

		} else if (record->type_class.rr_type == Type_NS) {
			Name name;
			if (!decodeName(offset, name, 0, maxRdOffset)) { printf("WTF 5?\n"); return DNS_STATUS_FORMAT_ERROR; }
			record->rr = new RR_NS(name);

		} else if (record->type_class.rr_type == Type_A) {
			if (record->rdLength > 4) { printf("WTF 6?\n"); return DNS_STATUS_FORMAT_ERROR; }
			static char A[4];
			buffer->pop((char*) &A, (size_t) offset, (size_t) 4);
			offset += record->rdLength;
			Samurai::IO::Net::InetAddress inet_addr;
			inet_addr.setRawAddress(A, 4, Samurai::IO::Net::InetAddress::IPv4);
			record->rr = new RR_A(inet_addr);

		} else if (record->type_class.rr_type == Type_AAAA) {
			if (record->rdLength > 16) { printf("WTF 7?\n"); return DNS_STATUS_FORMAT_ERROR; }
			static char A[16];
			buffer->pop((char*) &A, (size_t) offset, (size_t) record->rdLength);
			offset += record->rdLength;
			Samurai::IO::Net::InetAddress inet_addr;
			inet_addr.setRawAddress(A, record->rdLength, Samurai::IO::Net::InetAddress::IPv6);
			record->rr = new RR_AAAA(inet_addr);
		
		} else if (record->type_class.rr_type == Type_SOA) {
			Name primary;
			Name email;
			uint32_t serial;
			uint32_t refresh;
			uint32_t retry;
			uint32_t expire;
			int32_t ttl;
			if (!decodeName(offset, primary, 0, 0) ||
					!decodeName(offset, email, 0, 0) ||
					!decode32Bits(offset, serial) ||
					!decode32Bits(offset, refresh) ||
					!decode32Bits(offset, retry) ||
					!decode32Bits(offset, expire) ||
					!decodeS32Bits(offset, ttl))
				{ printf("WTF 8?\n"); return DNS_STATUS_FORMAT_ERROR; }
			
			record->rr = new RR_SOA(primary, email, serial, refresh, retry, expire, ttl);

		} else {
			// Ignore unknown data type
			printf("Unknown RR type: %d\n", (int) record->type_class.rr_type);
			offset += record->rdLength;
		}
	
		// printf("Adding RR type: %d\n", (int) record->type_class.rr_type);
		records.push_back(record);
		
	}
	
	return DNS_STATUS_OK;
}


Samurai::IO::Net::DNS::ResourceRecord* Samurai::IO::Net::DNS::Message::getRecord(Samurai::IO::Net::DNS::Name* name)
{
	QDBG("getRecord(%p) = '%s'\n", name, name ? name->toString() : "");
	
	if (!name) return 0;
	
	QDBG("Records: %d\n", (int) records.size());
	for (std::vector<Samurai::IO::Net::DNS::ResourceRecord*>::iterator it = records.begin(); it != records.end(); it++) {
		Samurai::IO::Net::DNS::ResourceRecord* record = (*it);
		QDBG("Record: '%s' == '%s', %d\n", record->name->toString(), name->toString(), (int) (uint16_t) record->type_class.rr_type);
		if ((*record->name) == *name /*&& record->type_class.rr_type == (uint16_t) Type_A*/)
		{
			QDBG("Match!\n");
			return record;
		}
	}
	return 0;
}

Samurai::IO::Net::DNS::ResourceRecord* Samurai::IO::Net::DNS::Message::getRecord(size_t index)
{
	if (index >= records.size() || !records.size()) return 0;
	return records[index];
}




