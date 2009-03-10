/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/crypto/digest/hashvalue.h>
#include <samurai/util/base32.h>

Samurai::Crypto::Digest::HashValue::HashValue() : m_data(0), m_size(0)
{
}

Samurai::Crypto::Digest::HashValue::HashValue(size_t size, uint8_t* data) : m_data(0), m_size(size)
{
	m_data = new uint8_t[m_size];
	memcpy(m_data, data, m_size);
}

Samurai::Crypto::Digest::HashValue::HashValue(size_t size) : m_data(0), m_size(size)
{
	m_data = new uint8_t[m_size];
	memset(m_data, 0, m_size);
}

Samurai::Crypto::Digest::HashValue::HashValue(Samurai::Crypto::Digest::HashValue* copy) : m_data(0), m_size(copy->m_size)
{
	m_data = new uint8_t[m_size];
	memcpy(m_data, copy->m_data, m_size);
}

Samurai::Crypto::Digest::HashValue::HashValue(const Samurai::Crypto::Digest::HashValue& copy) : m_data(0), m_size(copy.m_size)
{
	m_data = new uint8_t[m_size];
	memcpy(m_data, copy.m_data, m_size);
}

void Samurai::Crypto::Digest::HashValue::setData(const uint8_t* data)
{
	m_data = new uint8_t[m_size];
	memcpy(m_data, data, m_size);
}

Samurai::Crypto::Digest::HashValue::~HashValue()
{
	delete[] m_data; m_data = 0;
}

size_t Samurai::Crypto::Digest::HashValue::size() const
{
	return m_size;
}

uint8_t* Samurai::Crypto::Digest::HashValue::getData()
{
	return m_data;
}

bool Samurai::Crypto::Digest::HashValue::getFormattedString(enum Format format, char* buf, size_t buflen)
{
	buf[0] = 0;
	if (format == FormatHex) {
		if (buflen < (m_size*2)+1)
			return false;
		for (size_t n = 0; n < m_size; n++)
			sprintf(&buf[n*2], "%02x", (int) m_data[n]);
		
	} else  if (format == FormatBase32) {
		if (buflen < (m_size*5/8)+1)
			return false;
		base32_encode((unsigned char*) m_data, m_size, (char*) buf);
		buf[buflen] = 0;
	
	} else {
		return false;
	}
	buf[buflen] = 0;
	return true;
}

Samurai::Crypto::Digest::HashValue& Samurai::Crypto::Digest::HashValue::operator=(const HashValue& copy)
{
	memcpy(m_data, copy.m_data, m_size);
	return *this;
}

bool Samurai::Crypto::Digest::HashValue::operator==(const HashValue& h)
{
	if (h.m_size != m_size) return false;
	for (size_t n = 0; n < m_size; n++)
		if (h.m_data[n] != m_data[n]) return false;
	return true;
}

bool Samurai::Crypto::Digest::HashValue::operator!=(const HashValue& h)
{
	if (h.m_size != m_size) return true;
	for (size_t n = 0; n < m_size; n++)
		if (h.m_data[n] != m_data[n]) return true;
	return false;
}
