/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_HARDWARE_ADDRESS_H
#define HAVE_SAMURAI_HARDWARE_ADDRESS_H

namespace Samurai {
namespace IO {
namespace Net {

class HardwareAddress
{
	public:
		/**
		 * Create based on textual representation of
		 * the mac address (6 hexadecimal octets separated by colon).
		 */
		HardwareAddress(const char* text);
		HardwareAddress(const uint8_t octets[6]);
		
		virtual ~HardwareAddress();
		
		const char* getAddress() const;
		const uint8_t* getOctets() const;
		
	protected:
		uint8_t octets[6];
		char macaddr[18]; // "xx:xx:xx:xx:xx:xx"
};

}
}
}

#endif // HAVE_SAMURAI_HARDWARE_ADDRESS_H
