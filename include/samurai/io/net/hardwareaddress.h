/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_HARDWARE_ADDRESS_H
#define HAVE_SAMURAI_HARDWARE_ADDRESS_H

#include <stdint.h>
#include <span>

namespace Samurai {
namespace IO {
namespace Net {

class HardwareAddress
{
	public:
		/**
		 * Create based on textual representation of
		 * the mac address (6 hexadecimal octets separated by colon).
		 *
		 * An address that does not parse leaves the octets all zero, which
		 * getAddress() then renders as "00:00:00:00:00:00".
		 */
		HardwareAddress(const char* text);
		/* The '[6]' in a parameter is decoration - the compiler sees a plain
		 * pointer. A fixed-extent span is the length the caller must supply. */
		explicit HardwareAddress(std::span<const uint8_t, 6> octets);

		~HardwareAddress();

		const char* getAddress() const;
		std::span<const uint8_t, 6> getOctets() const;

	protected:
		/* Both are always set by every constructor, so getAddress() returns a
		 * terminated string and getOctets() six defined bytes. */
		uint8_t octets[6] = {};
		char macaddr[18] = {}; // "xx:xx:xx:xx:xx:xx"
};

}
}
}

#endif // HAVE_SAMURAI_HARDWARE_ADDRESS_H
