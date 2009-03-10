/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/crypto/digest/hash.h>
#include <samurai/util/base32.h>


Samurai::Crypto::Digest::Hash::Hash(size_t result_size, size_t block_size, bool big_bit_endian, bool big_byte_endian, size_t count_size) :
	m_size(result_size),
	m_block_size(block_size),
	m_current_block(0),
	m_current_block_index(0),
	m_file_size(0),
	m_big_bit_endian(big_bit_endian),
	m_big_byte_endian(big_byte_endian),
	m_finalized(false),
	m_finalized_value(m_size),
	m_count_size(count_size)
{
	m_current_block = new uint8_t[m_block_size];
}

Samurai::Crypto::Digest::Hash::~Hash()
{
	delete[] m_current_block;
}

void Samurai::Crypto::Digest::Hash::update(uint8_t* data, uint64_t length)
{
	if (!data || !length || m_finalized) return;
	
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
			hash(m_current_block, m_block_size);
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
		hash(m_current_block, m_block_size);
		memset(m_current_block, 0, m_block_size);
	}
	
	finalize_count();
	
	hash(m_current_block, m_block_size);
	
}

void Samurai::Crypto::Digest::Hash::finalize_count()
{
	uint8_t* pos = m_current_block + m_block_size - m_count_size;
	
	for (size_t j = 0; j < 8; ++j)
	{
		const size_t choose = (m_big_byte_endian ? (j % 8) : (7 - (j % 8)));
		pos[j + m_count_size - 8] = get_byte(choose, 8 * m_file_size);
	}
}

void Samurai::Crypto::Digest::Hash::set_finalized_value(uint8_t* data)
{
	if (m_finalized) return;
	
	m_finalized_value.setData(data);
}

