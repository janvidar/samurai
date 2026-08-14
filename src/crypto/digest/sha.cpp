/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/crypto/digest/sha.h>

#include <string.h>

/*
 * FIPS 180-4. Both algorithms are Merkle-Damgard over 64-byte blocks with a
 * 0x80 pad and a big-endian bit count, which the Hash base already does - see
 * its finalize() and finalize_count(). What is here is the compression
 * function of each and its initial state.
 */

namespace {

inline uint32_t rol32(uint32_t v, unsigned n)
{
	return (v << n) | (v >> (32 - n));
}

inline uint32_t ror32(uint32_t v, unsigned n)
{
	return (v >> n) | (v << (32 - n));
}

inline uint32_t beLoad32(const uint8_t* p)
{
	return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16)
	     | (static_cast<uint32_t>(p[2]) << 8)  |  static_cast<uint32_t>(p[3]);
}

inline void beStore32(uint8_t* p, uint32_t v)
{
	p[0] = static_cast<uint8_t>(v >> 24);
	p[1] = static_cast<uint8_t>(v >> 16);
	p[2] = static_cast<uint8_t>(v >> 8);
	p[3] = static_cast<uint8_t>(v);
}

/** SHA-256 round constants: the first 32 bits of the fractional parts of the
    cube roots of the first 64 primes. */
const uint32_t SHA256_K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

} // namespace

/* --- SHA-1 --------------------------------------------------------------- */

Samurai::Crypto::Digest::Sha1::Sha1()
	: Hash(SHA1_HASH_SIZE, SHA_BLOCK_SIZE, true, true, 8)
{
	reset();
}

Samurai::Crypto::Digest::Sha1::~Sha1() = default;

void Samurai::Crypto::Digest::Sha1::reset()
{
	m_state[0] = 0x67452301;
	m_state[1] = 0xefcdab89;
	m_state[2] = 0x98badcfe;
	m_state[3] = 0x10325476;
	m_state[4] = 0xc3d2e1f0;

	m_current_block_index = 0;
	m_file_size = 0;
	m_finalized = false;
	memset(m_current_block.data(), 0, m_current_block.size());
}

void Samurai::Crypto::Digest::Sha1::hash(uint8_t* block, size_t length)
{
	if (length != SHA_BLOCK_SIZE) return;

	uint32_t w[80];
	for (size_t i = 0; i < 16; i++)
		w[i] = beLoad32(block + i * 4);
	for (size_t i = 16; i < 80; i++)
		w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

	uint32_t a = m_state[0];
	uint32_t b = m_state[1];
	uint32_t c = m_state[2];
	uint32_t d = m_state[3];
	uint32_t e = m_state[4];

	for (size_t i = 0; i < 80; i++)
	{
		uint32_t f = 0;
		uint32_t k = 0;

		if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5a827999; }
		else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ed9eba1; }
		else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
		else             { f = b ^ c ^ d;                   k = 0xca62c1d6; }

		const uint32_t temp = rol32(a, 5) + f + e + k + w[i];
		e = d;
		d = c;
		c = rol32(b, 30);
		b = a;
		a = temp;
	}

	m_state[0] += a;
	m_state[1] += b;
	m_state[2] += c;
	m_state[3] += d;
	m_state[4] += e;
}

const Samurai::Crypto::Digest::HashValue& Samurai::Crypto::Digest::Sha1::digest()
{
	if (!m_finalized)
	{
		finalize();

		uint8_t out[SHA1_HASH_SIZE];
		for (size_t i = 0; i < 5; i++)
			beStore32(out + i * 4, m_state[i]);

		set_finalized_value(std::span<const uint8_t>(out, sizeof(out)));
		m_finalized = true;
	}
	return m_finalized_value;
}

/* --- SHA-256 ------------------------------------------------------------- */

Samurai::Crypto::Digest::Sha256::Sha256()
	: Hash(SHA256_HASH_SIZE, SHA_BLOCK_SIZE, true, true, 8)
{
	reset();
}

Samurai::Crypto::Digest::Sha256::~Sha256() = default;

void Samurai::Crypto::Digest::Sha256::reset()
{
	/* The first 32 bits of the fractional parts of the square roots of the
	   first eight primes. */
	m_state[0] = 0x6a09e667;
	m_state[1] = 0xbb67ae85;
	m_state[2] = 0x3c6ef372;
	m_state[3] = 0xa54ff53a;
	m_state[4] = 0x510e527f;
	m_state[5] = 0x9b05688c;
	m_state[6] = 0x1f83d9ab;
	m_state[7] = 0x5be0cd19;

	m_current_block_index = 0;
	m_file_size = 0;
	m_finalized = false;
	memset(m_current_block.data(), 0, m_current_block.size());
}

void Samurai::Crypto::Digest::Sha256::hash(uint8_t* block, size_t length)
{
	if (length != SHA_BLOCK_SIZE) return;

	uint32_t w[64];
	for (size_t i = 0; i < 16; i++)
		w[i] = beLoad32(block + i * 4);

	for (size_t i = 16; i < 64; i++)
	{
		const uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
		const uint32_t s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19)  ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}

	uint32_t a = m_state[0];
	uint32_t b = m_state[1];
	uint32_t c = m_state[2];
	uint32_t d = m_state[3];
	uint32_t e = m_state[4];
	uint32_t f = m_state[5];
	uint32_t g = m_state[6];
	uint32_t h = m_state[7];

	for (size_t i = 0; i < 64; i++)
	{
		const uint32_t S1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
		const uint32_t ch = (e & f) ^ (~e & g);
		const uint32_t temp1 = h + S1 + ch + SHA256_K[i] + w[i];
		const uint32_t S0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
		const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
		const uint32_t temp2 = S0 + maj;

		h = g;
		g = f;
		f = e;
		e = d + temp1;
		d = c;
		c = b;
		b = a;
		a = temp1 + temp2;
	}

	m_state[0] += a;
	m_state[1] += b;
	m_state[2] += c;
	m_state[3] += d;
	m_state[4] += e;
	m_state[5] += f;
	m_state[6] += g;
	m_state[7] += h;
}

const Samurai::Crypto::Digest::HashValue& Samurai::Crypto::Digest::Sha256::digest()
{
	if (!m_finalized)
	{
		finalize();

		uint8_t out[SHA256_HASH_SIZE];
		for (size_t i = 0; i < 8; i++)
			beStore32(out + i * 4, m_state[i]);

		set_finalized_value(std::span<const uint8_t>(out, sizeof(out)));
		m_finalized = true;
	}
	return m_finalized_value;
}
