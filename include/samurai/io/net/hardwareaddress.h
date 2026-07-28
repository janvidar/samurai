/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_HARDWARE_ADDRESS_H
#define HAVE_SAMURAI_HARDWARE_ADDRESS_H

#include <stdint.h>

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
		HardwareAddress(const uint8_t octets[6]);

		virtual ~HardwareAddress();

		const char* getAddress() const;
		const uint8_t* getOctets() const;

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
