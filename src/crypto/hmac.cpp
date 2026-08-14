/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/crypto/digest/sha.h>
#include <samurai/crypto/hmac.h>

#include <string.h>

#include <vector>

/*
 * RFC 2104: H(K xor opad, H(K xor ipad, message)).
 *
 * The two pads are what stop the construction being length-extendable, which
 * is the whole reason HMAC exists rather than H(key || message).
 */

namespace {

constexpr uint8_t IPAD = 0x36;
constexpr uint8_t OPAD = 0x5c;

using Samurai::Crypto::HmacAlgorithm;
using Samurai::Crypto::Digest::Hash;
using Samurai::Crypto::Digest::HashValue;
using Samurai::Crypto::Digest::Sha1;
using Samurai::Crypto::Digest::Sha256;
using Samurai::Crypto::Digest::SHA_BLOCK_SIZE;

std::unique_ptr<Hash> makeHash(HmacAlgorithm algorithm)
{
	if (algorithm == HmacAlgorithm::Sha256)
		return std::make_unique<Sha256>();
	return std::make_unique<Sha1>();
}

} // namespace

size_t Samurai::Crypto::hmacSize(HmacAlgorithm algorithm)
{
	return algorithm == HmacAlgorithm::Sha256
		? Digest::SHA256_HASH_SIZE
		: Digest::SHA1_HASH_SIZE;
}

class Samurai::Crypto::Hmac::Impl
{
	public:
		Impl(HmacAlgorithm algorithm, std::span<const uint8_t> key)
			: algorithm(algorithm)
			, inner(makeHash(algorithm))
			, value(Samurai::Crypto::hmacSize(algorithm))
		{
			/*
			 * A key longer than the block is replaced by its own digest, and a
			 * shorter one is zero-padded. Both are RFC 2104; the first is what
			 * makes an arbitrarily long key usable at all.
			 */
			block.assign(SHA_BLOCK_SIZE, 0);

			if (key.size() > SHA_BLOCK_SIZE)
			{
				std::unique_ptr<Hash> shrink = makeHash(algorithm);
				shrink->update(key);
				const std::span<const uint8_t> digested = shrink->digest().bytes();
				memcpy(block.data(), digested.data(), digested.size());
			}
			else if (!key.empty())
			{
				memcpy(block.data(), key.data(), key.size());
			}

			start();
		}

		void start()
		{
			finalized = false;
			inner->reset();

			uint8_t pad[SHA_BLOCK_SIZE];
			for (size_t i = 0; i < SHA_BLOCK_SIZE; i++)
				pad[i] = static_cast<uint8_t>(block[i] ^ IPAD);
			inner->update(std::span<const uint8_t>(pad, sizeof(pad)));
		}

		void update(std::span<const uint8_t> data)
		{
			if (finalized) return;
			inner->update(data);
		}

		const HashValue& digest()
		{
			if (finalized)
				return value;

			const std::span<const uint8_t> innerDigest = inner->digest().bytes();

			std::unique_ptr<Hash> outer = makeHash(algorithm);
			uint8_t pad[SHA_BLOCK_SIZE];
			for (size_t i = 0; i < SHA_BLOCK_SIZE; i++)
				pad[i] = static_cast<uint8_t>(block[i] ^ OPAD);

			outer->update(std::span<const uint8_t>(pad, sizeof(pad)));
			outer->update(innerDigest);

			value.setData(outer->digest().bytes());
			finalized = true;
			return value;
		}

		~Impl()
		{
			/*
			 * The padded key is the secret this object held. Clearing it is
			 * best effort - a compiler is entitled to elide a dead store - but
			 * the volatile write is not something it may remove.
			 */
			volatile uint8_t* p = block.data();
			for (size_t i = 0; i < block.size(); i++)
				p[i] = 0;
		}

		HmacAlgorithm algorithm;
		std::unique_ptr<Hash> inner;
		std::vector<uint8_t> block;
		HashValue value;
		bool finalized = false;
};

Samurai::Crypto::Hmac::Hmac(HmacAlgorithm algorithm, std::span<const uint8_t> key)
	: impl(std::make_unique<Impl>(algorithm, key))
{
}

Samurai::Crypto::Hmac::~Hmac() = default;

void Samurai::Crypto::Hmac::update(std::span<const uint8_t> data)
{
	impl->update(data);
}

void Samurai::Crypto::Hmac::update(std::string_view text)
{
	update(std::span<const uint8_t>(
		reinterpret_cast<const uint8_t*>(text.data()), text.size()));
}

const Samurai::Crypto::Digest::HashValue& Samurai::Crypto::Hmac::digest()
{
	return impl->digest();
}

void Samurai::Crypto::Hmac::reset()
{
	impl->start();
}

size_t Samurai::Crypto::Hmac::size() const
{
	return hmacSize(impl->algorithm);
}

Samurai::Crypto::Digest::HashValue
Samurai::Crypto::hmac(HmacAlgorithm algorithm, std::span<const uint8_t> key,
                      std::span<const uint8_t> message)
{
	Hmac mac(algorithm, key);
	mac.update(message);
	return mac.digest();
}

Samurai::Crypto::Digest::HashValue
Samurai::Crypto::hmacSha1(std::span<const uint8_t> key,
                          std::span<const uint8_t> message)
{
	return hmac(HmacAlgorithm::Sha1, key, message);
}

Samurai::Crypto::Digest::HashValue
Samurai::Crypto::hmacSha256(std::span<const uint8_t> key,
                            std::span<const uint8_t> message)
{
	return hmac(HmacAlgorithm::Sha256, key, message);
}

namespace {

std::span<const uint8_t> asBytes(std::string_view text)
{
	return std::span<const uint8_t>(
		reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

} // namespace

Samurai::Crypto::Digest::HashValue
Samurai::Crypto::hmacSha1(std::string_view key, std::string_view message)
{
	return hmacSha1(asBytes(key), asBytes(message));
}

Samurai::Crypto::Digest::HashValue
Samurai::Crypto::hmacSha256(std::string_view key, std::string_view message)
{
	return hmacSha256(asBytes(key), asBytes(message));
}
