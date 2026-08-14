/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_SHA_H
#define HAVE_SAMURAI_SHA_H

#include <samurai/crypto/digest/hash.h>

namespace Samurai {
namespace Crypto {
namespace Digest {

/* Digest sizes in bytes. */
inline constexpr size_t SHA1_HASH_SIZE   = 160 / 8;
inline constexpr size_t SHA256_HASH_SIZE = 256 / 8;

/** Both are Merkle-Damgard over 64-byte blocks. */
inline constexpr size_t SHA_BLOCK_SIZE = 64;

/*
 * SHA-1 and SHA-256.
 *
 * Implemented here rather than called through OpenSSL, although OpenSSL is a
 * mandatory dependency and offers both. The reasoning is the one random.h
 * already gives for asking the kernel directly: a primitive this basic should
 * not change when the TLS backend does, and Digest is otherwise a namespace of
 * implementations rather than of bindings - Tiger is here in full.
 *
 * Neither is a general recommendation. SHA-1 is present because protocols that
 * are still deployed require it - STUN short-term credentials name
 * HMAC-SHA1 as their baseline - and not because anything new should choose it.
 * Its collision resistance is broken; HMAC-SHA1 is not affected by that, which
 * is why it survives in protocols that use it only as a MAC.
 *
 * The base class already does everything these share: block accumulation, the
 * 0x80 pad and the big-endian bit count. A subclass supplies its compression
 * function and its initial state.
 */

class Sha1 final : public Hash
{
	public:
		Sha1();
		~Sha1() override;

		void reset() override;
		const HashValue& digest() override;

	protected:
		void hash(uint8_t* block, size_t length) override;

	private:
		uint32_t m_state[5];
};

class Sha256 final : public Hash
{
	public:
		Sha256();
		~Sha256() override;

		void reset() override;
		const HashValue& digest() override;

	protected:
		void hash(uint8_t* block, size_t length) override;

	private:
		uint32_t m_state[8];
};

}
}
}

#endif // HAVE_SAMURAI_SHA_H
