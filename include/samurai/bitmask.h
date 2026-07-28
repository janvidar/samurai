/*
 * Copyright (C) 2001-2009 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_BITMASK_H
#define HAVE_SAMURAI_BITMASK_H

#include <type_traits>

/**
 * Give a scoped enumeration the bitwise operators a flag set needs.
 *
 * A scoped enumeration does not convert to an integer, which is the point: it
 * stops an unrelated integer being passed where a flag set is expected. That
 * also removes the built-in bitwise operators, so a flag set has to say which
 * ones it wants.
 *
 * Expand this in the namespace that encloses the enumeration - for one nested
 * in a class, that is the namespace the class is in - so that unqualified uses
 * find the operators by argument-dependent lookup.
 *
 * any() is what replaces the old 'if (flags & Flag::X)': the result of & is
 * itself an enumeration and does not convert to bool.
 */
#define SAMURAI_DECLARE_BITMASK(E)                                             \
	constexpr E operator|(E a, E b) noexcept {                                 \
		using U = std::underlying_type_t<E>;                                   \
		return static_cast<E>(static_cast<U>(a) | static_cast<U>(b));          \
	}                                                                          \
	constexpr E operator&(E a, E b) noexcept {                                 \
		using U = std::underlying_type_t<E>;                                   \
		return static_cast<E>(static_cast<U>(a) & static_cast<U>(b));          \
	}                                                                          \
	constexpr E operator^(E a, E b) noexcept {                                 \
		using U = std::underlying_type_t<E>;                                   \
		return static_cast<E>(static_cast<U>(a) ^ static_cast<U>(b));          \
	}                                                                          \
	constexpr E operator~(E a) noexcept {                                      \
		using U = std::underlying_type_t<E>;                                   \
		return static_cast<E>(~static_cast<U>(a));                             \
	}                                                                          \
	constexpr E& operator|=(E& a, E b) noexcept { a = a | b; return a; }       \
	constexpr E& operator&=(E& a, E b) noexcept { a = a & b; return a; }       \
	constexpr E& operator^=(E& a, E b) noexcept { a = a ^ b; return a; }       \
	/** @return true if any bit is set. */                                     \
	constexpr bool any(E a) noexcept {                                         \
		return static_cast<std::underlying_type_t<E>>(a) != 0;                 \
	}                                                                          \
	/** @return true if every bit in 'bits' is set in 'a'. */                  \
	constexpr bool all(E a, E bits) noexcept { return (a & bits) == bits; }

#endif // HAVE_SAMURAI_BITMASK_H
