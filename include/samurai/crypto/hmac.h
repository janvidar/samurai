/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_HMAC_H
#define HAVE_SAMURAI_HMAC_H

#include <samurai/crypto/digest/hash.h>
#include <samurai/crypto/digest/hashvalue.h>

#include <memory>
#include <span>
#include <string_view>

namespace Samurai {
namespace Crypto {

/**
 * Keyed-hash message authentication, RFC 2104.
 *
 * Which digest to key it with. Both are here because deployed protocols name
 * them: STUN short-term credentials (RFC 8489 section 9.1) require
 * HMAC-SHA1 as the baseline and add HMAC-SHA256 alongside it.
 *
 * HMAC-SHA1 remains sound despite SHA-1's broken collision resistance - HMAC
 * does not rest on it - but nothing new should choose it where the peer can be
 * told to use something else.
 */
enum class HmacAlgorithm { Sha1, Sha256 };

/** The digest size an algorithm produces, in bytes. */
size_t hmacSize(HmacAlgorithm algorithm);

/**
 * An incremental HMAC.
 *
 * Use this for a message assembled in pieces; for one already in hand, the
 * free functions below are a single call.
 *
 * A key longer than the digest's block size is hashed down to one, and a
 * shorter key is zero-padded, both as RFC 2104 requires. An empty key is
 * allowed - it is a valid, if useless, key rather than a mistake to reject.
 */
class Hmac final
{
	public:
		Hmac(HmacAlgorithm algorithm, std::span<const uint8_t> key);
		~Hmac();

		/* Holds digest state that would be shared by a copy. */
		Hmac(const Hmac&) = delete;
		Hmac& operator=(const Hmac&) = delete;

		void update(std::span<const uint8_t> data);
		void update(std::string_view text);

		/**
		 * The finalized MAC, owned by this object and valid until it is reset
		 * or destroyed.
		 */
		const Digest::HashValue& digest();

		/** Start again with the same key. */
		void reset();

		size_t size() const;

	private:
		class Impl;
		std::unique_ptr<Impl> impl;
};

/* One-shot forms, which is what most callers want. */
Digest::HashValue hmac(HmacAlgorithm algorithm, std::span<const uint8_t> key,
                       std::span<const uint8_t> message);

Digest::HashValue hmacSha1(std::span<const uint8_t> key,
                           std::span<const uint8_t> message);

Digest::HashValue hmacSha256(std::span<const uint8_t> key,
                             std::span<const uint8_t> message);

/** For a caller whose key and message are text. */
Digest::HashValue hmacSha1(std::string_view key, std::string_view message);
Digest::HashValue hmacSha256(std::string_view key, std::string_view message);

}
}

#endif // HAVE_SAMURAI_HMAC_H
