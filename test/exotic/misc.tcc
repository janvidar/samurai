/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/net/hardwareaddress.h>
#include <samurai/io/net/bandwidth.h>
#include <samurai/util/bwestimation.h>
#include <string.h>

/*
 * EXO_TEST takes two macro arguments, so a braced initialiser containing
 * commas cannot appear inside one - the preprocessor would split it. The
 * fixtures therefore live at file scope.
 */
static const uint8_t hwaddr_octets_a[6] = { 0x00, 0x1b, 0x63, 0x84, 0x45, 0xe6 };
static const uint8_t hwaddr_octets_b[6] = { 0xde, 0xad, 0xbe, 0xef, 0x00, 0x01 };

EXO_TEST(hwaddr_from_octets,
{
	Samurai::IO::Net::HardwareAddress addr(hwaddr_octets_a);
	return strcmp(addr.getAddress(), "00:1b:63:84:45:e6") == 0;
});

EXO_TEST(hwaddr_from_octets_keeps_octets,
{
	Samurai::IO::Net::HardwareAddress addr(hwaddr_octets_b);
	return memcmp(addr.getOctets(), hwaddr_octets_b, 6) == 0;
});

EXO_TEST(hwaddr_from_text,
{
	Samurai::IO::Net::HardwareAddress addr("00:1b:63:84:45:e6");
	const uint8_t* o = addr.getOctets();
	return o[0] == 0x00 && o[1] == 0x1b && o[2] == 0x63
		&& o[3] == 0x84 && o[4] == 0x45 && o[5] == 0xe6;
});

EXO_TEST(hwaddr_from_text_round_trip,
{
	Samurai::IO::Net::HardwareAddress addr("de:ad:be:ef:00:01");
	return strcmp(addr.getAddress(), "de:ad:be:ef:00:01") == 0;
});

EXO_TEST(hwaddr_from_text_uppercase,
{
	Samurai::IO::Net::HardwareAddress addr("AA:BB:CC:DD:EE:FF");
	const uint8_t* o = addr.getOctets();
	return o[0] == 0xaa && o[5] == 0xff;
});

/* An address that does not parse must still leave a terminated string and six
 * defined octets behind, rather than whatever was on the stack. */
EXO_TEST(hwaddr_from_garbage_is_zero,
{
	Samurai::IO::Net::HardwareAddress addr("not-a-mac-address");
	const uint8_t* o = addr.getOctets();
	return strcmp(addr.getAddress(), "00:00:00:00:00:00") == 0
		&& o[0] == 0 && o[1] == 0 && o[2] == 0
		&& o[3] == 0 && o[4] == 0 && o[5] == 0;
});

EXO_TEST(hwaddr_from_null_is_zero,
{
	Samurai::IO::Net::HardwareAddress addr((const char*) 0);
	return strcmp(addr.getAddress(), "00:00:00:00:00:00") == 0;
});

EXO_TEST(hwaddr_from_truncated_text_is_zero,
{
	Samurai::IO::Net::HardwareAddress addr("00:1b:63");
	return strcmp(addr.getAddress(), "00:00:00:00:00:00") == 0;
});

/* A freshly built estimator has transferred nothing, so it must read zero
 * rather than whatever its 'last' member happened to contain. */
EXO_TEST(rate_estimator_starts_at_zero,
{
	Samurai::Util::RateEstimator est;
	return est.getBps() == 0;
});

EXO_TEST(rate_estimator_zero_after_add_of_nothing,
{
	Samurai::Util::RateEstimator est;
	est.add(0);
	return est.getBps() == 0;
});

/* The manager is a singleton reached from every socket constructor, so its
 * counters have to start defined. */
EXO_TEST(bandwidth_manager_instance_exists,
{
	return Samurai::IO::Net::BandwidthManager::getInstance() != 0;
});

EXO_TEST(bandwidth_manager_starts_at_zero,
{
	Samurai::IO::Net::BandwidthManager mgr;
	return mgr.getSendBps() == 0 && mgr.getRecvBps() == 0;
});
