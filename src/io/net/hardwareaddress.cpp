/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/hardwareaddress.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <format>

namespace {

void formatOctets(char (&out)[18], const uint8_t (&octets)[6])
{
	char* at = out;
	for (size_t n = 0; n < 6; n++)
		at = std::format_to(at, "{}{:02x}", n ? ":" : "", octets[n]);
	*at = 0;
}

}

Samurai::IO::Net::HardwareAddress::HardwareAddress(const char* text)
{
	/* sscanf's %x writes an unsigned int, so it cannot target the octets. */
	unsigned parsed[6] = {};
	if (text && sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x",
		&parsed[0], &parsed[1], &parsed[2],
		&parsed[3], &parsed[4], &parsed[5]) == 6)
	{
		for (size_t n = 0; n < 6; n++)
			octets[n] = (uint8_t) parsed[n];
	}
	formatOctets(macaddr, octets);
}

Samurai::IO::Net::HardwareAddress::HardwareAddress(std::span<const uint8_t, 6> octets_)
{
	std::copy(octets_.begin(), octets_.end(), octets);
	formatOctets(macaddr, octets);
}

Samurai::IO::Net::HardwareAddress::~HardwareAddress()
{
}

const char* Samurai::IO::Net::HardwareAddress::getAddress() const
{
	return macaddr;
}
std::span<const uint8_t, 6> Samurai::IO::Net::HardwareAddress::getOctets() const
{
	return octets;
}

