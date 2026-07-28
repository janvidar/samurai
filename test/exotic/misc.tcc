/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/net/hardwareaddress.h>
#include <samurai/io/net/bandwidth.h>
#include <samurai/util/bwestimation.h>
#include <samurai/io/file.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/util/format.h>
#include <samurai/timestamp.h>
#include <string>
#include <algorithm>
#include <span>
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
	return std::ranges::equal(addr.getOctets(), hwaddr_octets_b);
});

EXO_TEST(hwaddr_from_text,
{
	Samurai::IO::Net::HardwareAddress addr("00:1b:63:84:45:e6");
	const auto o = addr.getOctets();
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
	const auto o = addr.getOctets();
	return o[0] == 0xaa && o[5] == 0xff;
});

/* An address that does not parse must still leave a terminated string and six
 * defined octets behind, rather than whatever was on the stack. */
EXO_TEST(hwaddr_from_garbage_is_zero,
{
	Samurai::IO::Net::HardwareAddress addr("not-a-mac-address");
	const auto o = addr.getOctets();
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

/* ------------------------------------------------------------------------- */
/* Bitmask operators on the scoped flag enums                                */
/*                                                                           */
/* A scoped enum has no built-in bitwise operators and does not convert to    */
/* bool, so a flag set has to opt in; see samurai/bitmask.h.                 */
/* ------------------------------------------------------------------------- */

EXO_TEST(bitmask_or_combines,
{
	using Mode = Samurai::IO::File::Mode;
	const Mode m = Mode::Write | Mode::Truncate;
	return any(m & Mode::Write) && any(m & Mode::Truncate);
});

EXO_TEST(bitmask_and_excludes_unset,
{
	using Mode = Samurai::IO::File::Mode;
	const Mode m = Mode::Write | Mode::Truncate;
	return !any(m & Mode::Read) && !any(m & Mode::Append);
});

EXO_TEST(bitmask_all_requires_every_bit,
{
	using Mode = Samurai::IO::File::Mode;
	const Mode m = Mode::Write | Mode::Truncate;
	return all(m, Mode::Write | Mode::Truncate)
		&& !all(m, Mode::Write | Mode::Read);
});

EXO_TEST(bitmask_or_assign,
{
	using Mode = Samurai::IO::File::Mode;
	Mode m = Mode::Read;
	m |= Mode::Append;
	return any(m & Mode::Read) && any(m & Mode::Append);
});

EXO_TEST(bitmask_and_assign_masks,
{
	using Mode = Samurai::IO::File::Mode;
	Mode m = Mode::Read | Mode::Append | Mode::Paranoid;
	m &= Mode::Read | Mode::Append;
	return any(m & Mode::Read) && any(m & Mode::Append) && !any(m & Mode::Paranoid);
});

EXO_TEST(bitmask_complement_clears,
{
	using Mode = Samurai::IO::File::Mode;
	Mode m = Mode::Read | Mode::Append;
	m &= ~Mode::Append;
	return any(m & Mode::Read) && !any(m & Mode::Append);
});

EXO_TEST(bitmask_empty_set_is_not_any,
{
	using Trig = Samurai::IO::Net::SocketMonitor::Triggers;
	return !any(Trig::None) && any(Trig::Read);
});

EXO_TEST(bitmask_triggers_combine,
{
	using Trig = Samurai::IO::Net::SocketMonitor::Triggers;
	const Trig t = Trig::Read | Trig::Write;
	return any(t & Trig::Read) && any(t & Trig::Write) && !any(t & Trig::Error);
});

/* The operators are constant expressions, so a flag set can bound an array. */
EXO_TEST(bitmask_is_constexpr,
{
	using Mode = Samurai::IO::File::Mode;
	static_assert(any(Mode::Write | Mode::Read), "operators must be constexpr");
	static_assert(!any(Mode::Write & Mode::Read), "distinct flags must not overlap");
	return true;
});

/* ------------------------------------------------------------------------- */
/* Byte count formatting                                                     */
/* ------------------------------------------------------------------------- */

EXO_TEST(formatsize_bytes,
{
	return Samurai::Util::formatSize(512) == " 512  B";
});

EXO_TEST(formatsize_exact_kilobyte,
{
	return Samurai::Util::formatSize(1024) == "   1 KB";
});

EXO_TEST(formatsize_exact_megabyte,
{
	return Samurai::Util::formatSize(1024ULL * 1024) == "   1 MB";
});

EXO_TEST(formatsize_fractional,
{
	return Samurai::Util::formatSize(1536) == "1.50 KB";
});

EXO_TEST(formatsize_zero,
{
	return Samurai::Util::formatSize(0) == "   0  B";
});

/*
 * The unit has to advance at the threshold, not one step past it. Selecting on
 * "size > next_base" while dividing by "next_base / 1024" left the result one
 * unit low, so a megabyte read as "1024 KB".
 */
EXO_TEST(formatsize_megabyte_is_not_reported_in_kilobytes,
{
	const std::string s = Samurai::Util::formatSize(1024ULL * 1024);
	return s.find("KB") == std::string::npos && s.find("MB") != std::string::npos;
});

EXO_TEST(formatsize_gigabyte,
{
	return Samurai::Util::formatSize(1024ULL * 1024 * 1024) == "   1 GB";
});

EXO_TEST(formatsize_just_below_a_kilobyte,
{
	return Samurai::Util::formatSize(1023) == "1023  B";
});

EXO_TEST(formatsize_two_kilobytes_is_exact,
{
	return Samurai::Util::formatSize(2048) == "   2 KB";
});

/*
 * Past the last unit the multiplier must stop. Continuing would overflow the
 * divisor to zero, which never terminates and reads off the end of the table.
 */
EXO_TEST(formatsize_max_terminates_in_exabytes,
{
	const std::string s = Samurai::Util::formatSize(UINT64_MAX);
	return s.find("EB") != std::string::npos;
});

/*
 * Two calls in one expression must not interfere. The previous form returned a
 * pointer into a single function-local static, so both read back the same text.
 */
EXO_TEST(formatsize_two_calls_are_independent,
{
	const std::string a = Samurai::Util::formatSize(1024);
	const std::string b = Samurai::Util::formatSize(1024ULL * 1024);
	return a != b && a == "   1 KB" && b == "   1 MB";
});

/* ------------------------------------------------------------------------- */
/* TimeStamp                                                                 */
/* ------------------------------------------------------------------------- */

EXO_TEST(timestamp_format_is_applied,
{
	Samurai::TimeStamp ts((time_t) 0);
	/* %s is seconds since the epoch, so this is independent of the time zone. */
	return ts.getTime("%s") == "0";
});

EXO_TEST(timestamp_default_format_is_not_empty,
{
	Samurai::TimeStamp ts;
	return !ts.getTime().empty();
});

/* The same static-buffer hazard as above. */
EXO_TEST(timestamp_two_calls_are_independent,
{
	Samurai::TimeStamp early((time_t) 0);
	Samurai::TimeStamp later((time_t) 1000000);
	const std::string a = early.getTime("%s");
	const std::string b = later.getTime("%s");
	return a == "0" && b == "1000000";
});
