/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/hardwareaddress.h>

Samurai::IO::Net::HardwareAddress::HardwareAddress(const char* text)
{
	(void) text;
	// memcpy(macaddr, text, 18);
	// sscanf(macaddr, "%02x:%02x:%02x:%02x:%02x:%02x", (uint8_t) octets[0], (uint8_t) octets[1], (uint8_t) octets[2], (uint8_t) octets[3], (uint8_t) octets[4], (uint8_t) octets[5]);
}

Samurai::IO::Net::HardwareAddress::HardwareAddress(const uint8_t octets_[6])
{
	memcpy(octets, octets_, 6);
	snprintf(macaddr, 18, "%02x:%02x:%02x:%02x:%02x:%02x", octets[0], octets[1], octets[2], octets[3], octets[4], octets[5]);
}

Samurai::IO::Net::HardwareAddress::~HardwareAddress()
{
}

const char* Samurai::IO::Net::HardwareAddress::getAddress() const
{
	return macaddr;
}
const uint8_t* Samurai::IO::Net::HardwareAddress::getOctets() const
{
	return octets;
}

