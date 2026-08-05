/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/crypto/digest/hash.h>
#include <samurai/util/base32.h>


Samurai::Crypto::Digest::Hash::Hash(size_t result_size, size_t block_size, bool big_bit_endian, bool big_byte_endian, size_t count_size) :
	m_size(result_size),
	m_block_size(block_size),
	/* Sized from the constructor parameter, not the member, so this does not
	   depend on the order the members are declared in. */
	m_current_block(block_size),
	m_current_block_index(0),
	m_file_size(0),
	m_big_bit_endian(big_bit_endian),
	m_big_byte_endian(big_byte_endian),
	m_finalized(false),
	m_finalized_value(m_size),
	m_count_size(count_size)
{
}

Samurai::Crypto::Digest::Hash::~Hash() = default;

void Samurai::Crypto::Digest::Hash::update(std::span<const uint8_t> input)
{
	const uint8_t* data_ = input.data();
	const size_t length = input.size();

	if (!data_ || !length || m_finalized) return;

	/*
	 * With no room to accumulate into there is nothing to copy, so the loop
	 * below would compute a zero length, never advance and spin forever. The
	 * subtraction would also wrap if the index ever passed the block size.
	 */
	if (m_current_block_index >= m_block_size) return;

	const uint8_t* data = data_;
	size_t offset = 0;
	while (offset < length)
	{
		size_t blocklen = length - offset;
		if (blocklen > m_block_size - m_current_block_index) blocklen = m_block_size - m_current_block_index;
		
		memcpy(&m_current_block[m_current_block_index], &data[offset], blocklen);
		
		m_current_block_index += blocklen;
		offset += blocklen;
		
		if (m_current_block_index == m_block_size)
		{
			m_current_block_index = 0;
			hash(m_current_block.data(), m_block_size);
		}
	}
	m_file_size += length;
}


void Samurai::Crypto::Digest::Hash::finalize()
{
	if (m_finalized) return;
	
	m_current_block[m_current_block_index] = (m_big_bit_endian ? 0x80 : 0x01);
	for(uint32_t j = m_current_block_index + 1; j < m_block_size; ++j)
		m_current_block[j] = 0;
	
	if (m_current_block_index >= m_block_size - m_count_size)
	{
		hash(m_current_block.data(), m_block_size);
		memset(m_current_block.data(), 0, m_block_size);
	}
	
	finalize_count();
	
	hash(m_current_block.data(), m_block_size);
	
}

void Samurai::Crypto::Digest::Hash::finalize_count()
{
	if (m_block_size < 8) return;

	uint8_t* pos = m_current_block.data() + m_block_size - 8;

	for (size_t j = 0; j < 8; ++j)
	{
		const size_t choose = (m_big_byte_endian ? j : (7 - j));
		pos[j] = get_byte(choose, 8 * m_file_size);
	}
}

bool Samurai::Crypto::Digest::Hash::set_finalized_value(std::span<const uint8_t> data)
{
	if (m_finalized) return false;

	return m_finalized_value.setData(data);
}

