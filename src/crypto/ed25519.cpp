/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/crypto/ed25519.h>

#include <openssl/evp.h>

#include <memory>

namespace {

struct KeyFree { void operator()(EVP_PKEY* key) const { EVP_PKEY_free(key); } };
struct ContextFree { void operator()(EVP_MD_CTX* ctx) const { EVP_MD_CTX_free(ctx); } };

}

bool Samurai::Crypto::ed25519Verify(const Ed25519PublicKey& key, std::span<const uint8_t> message,
                                    const Ed25519Signature& signature)
{
	std::unique_ptr<EVP_PKEY, KeyFree> pkey(
		EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, key.data(), key.size()));
	std::unique_ptr<EVP_MD_CTX, ContextFree> ctx(EVP_MD_CTX_new());
	if (!pkey || !ctx) return false;

	/* Ed25519 is one shot: no digest is named, and the message goes in whole. */
	if (EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, pkey.get()) != 1) return false;
	return EVP_DigestVerify(ctx.get(), signature.data(), signature.size(),
	                        message.data(), message.size()) == 1;
}

bool Samurai::Crypto::ed25519Verify(const Ed25519PublicKey& key, std::string_view message,
                                    const Ed25519Signature& signature)
{
	return ed25519Verify(key, std::span<const uint8_t>(
		reinterpret_cast<const uint8_t*>(message.data()), message.size()), signature);
}

std::optional<Samurai::Crypto::Ed25519Signature> Samurai::Crypto::ed25519Sign(
	const Ed25519Seed& seed, std::string_view message)
{
	std::unique_ptr<EVP_PKEY, KeyFree> pkey(
		EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seed.data(), seed.size()));
	std::unique_ptr<EVP_MD_CTX, ContextFree> ctx(EVP_MD_CTX_new());
	if (!pkey || !ctx) return std::nullopt;

	Ed25519Signature out {};
	size_t length = out.size();
	if (EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, pkey.get()) != 1
	    || EVP_DigestSign(ctx.get(), out.data(), &length,
	                      reinterpret_cast<const unsigned char*>(message.data()), message.size()) != 1
	    || length != out.size())
		return std::nullopt;
	return out;
}

std::optional<Samurai::Crypto::Ed25519PublicKey> Samurai::Crypto::ed25519PublicKey(const Ed25519Seed& seed)
{
	std::unique_ptr<EVP_PKEY, KeyFree> pkey(
		EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seed.data(), seed.size()));
	if (!pkey) return std::nullopt;

	Ed25519PublicKey out {};
	size_t length = out.size();
	if (EVP_PKEY_get_raw_public_key(pkey.get(), out.data(), &length) != 1 || length != out.size())
		return std::nullopt;
	return out;
}
