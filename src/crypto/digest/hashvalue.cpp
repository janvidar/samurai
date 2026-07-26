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
	/* NOTE: This used to allocate a fresh buffer on every call while dropping
	   the previous one on the floor, leaking m_size bytes per call. m_size is
	   fixed at construction, so the existing buffer can just be reused.
	   Everything that finalizes a hash arrives here through
	   Hash::set_finalized_value(), so every Tiger::digest() leaked. */
	if (!m_data)
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
	if (!buf || buflen == 0)
		return false;

	buf[0] = 0;
	if (format == FormatHex) {
		/* Two characters per byte, plus the terminator. */
		if (buflen < (m_size*2)+1)
			return false;
		for (size_t n = 0; n < m_size; n++)
			sprintf(&buf[n*2], "%02x", (int) m_data[n]);
		buf[m_size*2] = 0;

	} else  if (format == FormatBase32) {
		/* NOTE: base32 expands rather than contracts - every 5 bits become
		   one character, so the output is ceil(m_size*8/5) characters plus a
		   terminator. This test used to read (m_size*5/8)+1, the inverse
		   ratio, which is far too small: for a 24 byte digest it accepted a
		   16 byte buffer while base32_encode() went on to write 40 bytes.
		   base32_encode() takes no length argument, so the caller's buffer is
		   all that stands between it and the rest of the stack. */
		if (buflen < ((m_size*8 + 4) / 5) + 1)
			return false;
		base32_encode((unsigned char*) m_data, m_size, (char*) buf);

	} else {
		return false;
	}

	/* NOTE: deliberately no buf[buflen] = 0 here. That is one byte past the
	   end of a buflen-sized buffer, and it used to run on every call - twice
	   on the base32 path. Both encoders terminate their own output. */
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
