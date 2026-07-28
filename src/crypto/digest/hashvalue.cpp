/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/crypto/digest/hashvalue.h>
#include <samurai/util/base32.h>
#include <span>
#include <stdio.h>
#include <string.h>
#include <algorithm>

Samurai::Crypto::Digest::HashValue::HashValue() = default;

Samurai::Crypto::Digest::HashValue::HashValue(size_t size, const uint8_t* data)
	: m_data(data, data + size)
{
}

Samurai::Crypto::Digest::HashValue::HashValue(size_t size)
	: m_data(size, 0)
{
}

Samurai::Crypto::Digest::HashValue::HashValue(Samurai::Crypto::Digest::HashValue* copy)
	: m_data(copy->m_data)
{
}

bool Samurai::Crypto::Digest::HashValue::setData(std::span<const uint8_t> data)
{
	/* The size is fixed at construction, so the existing storage is reused. */
	if (data.size() != m_data.size()) return false;

	std::copy(data.begin(), data.end(), m_data.begin());
	return true;
}

size_t Samurai::Crypto::Digest::HashValue::size() const
{
	return m_data.size();
}

uint8_t* Samurai::Crypto::Digest::HashValue::getData()
{
	return m_data.data();
}

const uint8_t* Samurai::Crypto::Digest::HashValue::getData() const
{
	return m_data.data();
}

bool Samurai::Crypto::Digest::HashValue::getFormattedString(Format format, char* buf, size_t buflen) const
{
	if (!buf || buflen == 0)
		return false;

	const size_t size = m_data.size();

	buf[0] = 0;
	if (format == Format::Hex) {
		/* Two characters per byte, plus the terminator. */
		if (buflen < (size*2)+1)
			return false;
		for (size_t n = 0; n < size; n++)
			snprintf(&buf[n*2], 3, "%02x", (unsigned) m_data[n]);
		buf[size*2] = 0;

	} else  if (format == Format::Base32) {
		/* NOTE: base32 expands rather than contracts - every 5 bits become
		   one character, so the output is ceil(size*8/5) characters plus a
		   terminator. */
		if (buflen < ((size*8 + 4) / 5) + 1)
			return false;
		if (!Samurai::Util::base32_encode(m_data, std::span<char>(buf, buflen)))
			return false;

	} else {
		return false;
	}

	/* NOTE: no buf[buflen] = 0 here - that is one byte past the end of a
	   buflen-sized buffer. Both encoders terminate their own output. */
	return true;
}

/* operator!= is synthesised from this one. */
bool Samurai::Crypto::Digest::HashValue::operator==(const HashValue& h) const
{
	return m_data == h.m_data;
}
