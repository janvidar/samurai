/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_QUICKDC_HASH_BASE_H
#define HAVE_QUICKDC_HASH_BASE_H

#include <samurai/samurai.h>
#include <samurai/crypto/digest/hashvalue.h>

namespace Samurai {
namespace Crypto {
namespace Digest {

/**
 * Base class and interface for a hash algorithm.
 */
class Hash {
	public:
		/**
		 * @param result_size size of the resulting digest in bytes (not bits).
		 * @param block_size  internal block size of digest algorithm in bytes.
		 * @param big_byte_endian byte-endianness encoding (depends on algorithm)
		 * @param big_byte_endian bit-endianness encoding (depends on algorithm)
		 * @param count_size padding size (depends on algorithm)
		 */
		Hash(size_t result_size, size_t block_size, bool big_bit_endian, bool big_byte_endian, size_t count_size);
		
		virtual ~Hash();

		/**
		 * Resets the internal digest to the starting point.
		 */
		virtual void reset() = 0;

		/**
		 * Returns the size of the produced digest (in raw bytes).
		 */
		size_t size() const { return m_size; };
		
		/**
		 * Returns a pointer to the internal raw data of the digest.
		 * NOTE: Do not read more than size() bytes from this!
		 * FIXME: Do not return a pointer, but rather a const reference!
		 */
		virtual HashValue* digest() = 0;
		
		/**
		 * Update/append the internal digest.
		 */
		virtual void update(uint8_t* data, uint64_t length);
		
		
		uint64_t getFileSize() { return m_file_size; }
		
	protected:
		/**
		 * Internal block hash.
		 */
		virtual void hash(uint8_t* block, size_t size) = 0;
		
		/**
		 * finalize + add padding.
		 */
		virtual void finalize();
		
		/**
		 * Internal method to set the finalized HashValue.
		 * 'data' must be size() long.
		 */
		void set_finalized_value(uint8_t* data);
		
	private:
		
		/**
		 * Finalize padding.
		 */
		virtual void finalize_count();
		
	protected:
		const size_t m_size;
		const size_t m_block_size;
		
		uint8_t* m_current_block;
		size_t   m_current_block_index;
		uint64_t m_file_size;
		
		/* internal hash state info */
		bool m_big_bit_endian;
		bool m_big_byte_endian;
		bool m_finalized;
		HashValue m_finalized_value;
		const size_t m_count_size;
};


/* MISC: */
inline uint64_t make_uint64_t(uint8_t u0, uint8_t u1, uint8_t u2, uint8_t u3,
							  uint8_t u4, uint8_t u5, uint8_t u6, uint8_t u7)
{
	return (uint64_t)(
					((uint64_t)u0 << 56) | ((uint64_t) u1 << 48) |
					((uint64_t)u2 << 40) | ((uint64_t) u3 << 32) |
					((uint64_t)u4 << 24) | ((uint64_t) u5 << 16) |
					((uint64_t)u6 <<  8) | u7);
}

template<typename T> inline uint8_t get_byte(size_t offset, T input)
{
	return (uint8_t) (input >> ((sizeof(T) - 1 - (offset & (sizeof(T) - 1))) << 3));
}


}
}
}

#endif // HAVE_QUICKDC_HASH_BASE_H

