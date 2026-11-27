/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_ED25519_H
#define HAVE_SAMURAI_ED25519_H

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace Samurai {
namespace Crypto {

/** Ed25519 (RFC 8032) as a public key, a signature and the private seed hold it. */
using Ed25519PublicKey = std::array<uint8_t, 32>;
using Ed25519Signature = std::array<uint8_t, 64>;
using Ed25519Seed      = std::array<uint8_t, 32>;

/** Whether 'signature' is 'key''s over exactly 'message'. */
bool ed25519Verify(const Ed25519PublicKey& key, std::span<const uint8_t> message,
                   const Ed25519Signature& signature);

bool ed25519Verify(const Ed25519PublicKey& key, std::string_view message,
                   const Ed25519Signature& signature);

/**
 * A signature by the key 'seed' makes, for a tool that signs and for tests.
 * Nothing that only verifies needs it. Empty if the TLS library refused.
 */
std::optional<Ed25519Signature> ed25519Sign(const Ed25519Seed& seed, std::string_view message);

/** The public key 'seed' makes; empty if the TLS library refused. */
std::optional<Ed25519PublicKey> ed25519PublicKey(const Ed25519Seed& seed);

}
}

#endif // HAVE_SAMURAI_ED25519_H
