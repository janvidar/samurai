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
#include <samurai/stdc.h>
#include <time.h>
#include <unistd.h>
#include <samurai/messagehandler.h>
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

/* ------------------------------------------------------------------------- */
/* Convert                                                                   */
/*                                                                           */
/* Only to_uint16 was reached before, and only indirectly through URL port    */
/* parsing. The saturation arithmetic was never executed by any test.        */
/* ------------------------------------------------------------------------- */

EXO_TEST(convert_to_int64_plain,
{
	return Samurai::Util::Convert::to_int64("12345") == 12345;
});

EXO_TEST(convert_to_int64_negative,
{
	return Samurai::Util::Convert::to_int64("-12345") == -12345;
});

EXO_TEST(convert_to_int64_explicit_plus,
{
	return Samurai::Util::Convert::to_int64("+42") == 42;
});

EXO_TEST(convert_to_int64_stops_at_first_non_digit,
{
	return Samurai::Util::Convert::to_int64("123abc") == 123
		&& Samurai::Util::Convert::to_int64("7.5") == 7;
});

EXO_TEST(convert_to_int64_empty_and_garbage,
{
	return Samurai::Util::Convert::to_int64("") == 0
		&& Samurai::Util::Convert::to_int64("abc") == 0
		&& Samurai::Util::Convert::to_int64("-") == 0;
});

EXO_TEST(convert_to_int64_limits,
{
	return Samurai::Util::Convert::to_int64("9223372036854775807") == INT64_MAX
		&& Samurai::Util::Convert::to_int64("-9223372036854775808") == INT64_MIN;
});

/* Past the limit it saturates rather than wrapping. */
EXO_TEST(convert_to_int64_saturates_positive,
{
	return Samurai::Util::Convert::to_int64("9223372036854775808") == INT64_MAX
		&& Samurai::Util::Convert::to_int64("99999999999999999999999") == INT64_MAX;
});

EXO_TEST(convert_to_int64_saturates_negative,
{
	return Samurai::Util::Convert::to_int64("-9223372036854775809") == INT64_MIN
		&& Samurai::Util::Convert::to_int64("-99999999999999999999999") == INT64_MIN;
});

EXO_TEST(convert_to_uint64_plain,
{
	return Samurai::Util::Convert::to_uint64("12345") == 12345u;
});

EXO_TEST(convert_to_uint64_limit,
{
	return Samurai::Util::Convert::to_uint64("18446744073709551615") == UINT64_MAX;
});

EXO_TEST(convert_to_uint64_saturates,
{
	return Samurai::Util::Convert::to_uint64("18446744073709551616") == UINT64_MAX
		&& Samurai::Util::Convert::to_uint64("99999999999999999999999") == UINT64_MAX;
});

/* Unsigned takes no sign, so a negative string is rejected outright. */
EXO_TEST(convert_to_uint64_rejects_negative,
{
	return Samurai::Util::Convert::to_uint64("-1") == 0u;
});

EXO_TEST(convert_to_int32_plain_and_limits,
{
	return Samurai::Util::Convert::to_int32("1000") == 1000
		&& Samurai::Util::Convert::to_int32("-1000") == -1000
		&& Samurai::Util::Convert::to_int32("2147483647") == INT32_MAX;
});

EXO_TEST(convert_to_uint16_accepts_digits_only,
{
	return Samurai::Util::Convert::to_uint16("8080") == 8080
		&& Samurai::Util::Convert::to_uint16("0") == 0;
});

/* Unlike the others, to_uint16 rejects a string that is not all digits rather
   than stopping at the first one - url.cpp relies on that to reject a port. */
EXO_TEST(convert_to_uint16_rejects_trailing_junk,
{
	return Samurai::Util::Convert::to_uint16("80a") == 0
		&& Samurai::Util::Convert::to_uint16("") == 0
		&& Samurai::Util::Convert::to_uint16("-80") == 0
		&& Samurai::Util::Convert::to_uint16(" 80") == 0;
});

EXO_TEST(convert_to_uint16_rejects_out_of_range,
{
	return Samurai::Util::Convert::to_uint16("65535") == 65535
		&& Samurai::Util::Convert::to_uint16("65536") == 0
		&& Samurai::Util::Convert::to_uint16("70000") == 0;
});

/* ------------------------------------------------------------------------- */
/* MessageHandler                                                            */
/*                                                                           */
/* A MessageListener registers itself with the singleton from its            */
/* constructor, so these listeners filter on a message id of their own -      */
/* anything else in the suite that listens would otherwise see these, and     */
/* these would see its.                                                      */
/* ------------------------------------------------------------------------- */

namespace {

class CountingListener final : public Samurai::MessageListener
{
	public:
		explicit CountingListener(size_t id) : wanted(id) { }

		bool EventMessage(const Samurai::Message* msg) override
		{
			if (!msg || msg->getID() != wanted) return false;

			seen++;
			last_arg1 = msg->getArg1();
			last_arg2 = msg->getArg2();
			last_data = msg->getData();
			return true;
		}

		size_t seen = 0;
		size_t last_arg1 = 0;
		size_t last_arg2 = 0;
		void* last_data = nullptr;

	private:
		size_t wanted;
};

/* Posts one further message the first time it is called, which is the
   carry-over path: a message posted while process() is running belongs to the
   next pass, not this one. */
class RepostingListener final : public Samurai::MessageListener
{
	public:
		RepostingListener(size_t id, size_t repost_as) : wanted(id), repost(repost_as) { }

		bool EventMessage(const Samurai::Message* msg) override
		{
			if (!msg) return false;

			if (msg->getID() == wanted && !reposted)
			{
				reposted = true;
				Samurai::MessageHandler::getInstance()->postMessage(repost, nullptr, 0, 0);
				return true;
			}

			if (msg->getID() == repost) { saw_repost = true; return true; }
			return false;
		}

		bool reposted = false;
		bool saw_repost = false;

	private:
		size_t wanted;
		size_t repost;
};

}

EXO_TEST(messagehandler_instance_exists,
{
	return Samurai::MessageHandler::getInstance() != nullptr;
});

/* Nothing queued must be a safe no-op, not a pop from an empty deque. */
EXO_TEST(messagehandler_process_of_an_empty_queue_is_safe,
{
	Samurai::MessageHandler::getInstance()->process();
	return true;
});

EXO_TEST(messagehandler_delivers_a_posted_message,
{
	CountingListener listener(0x51000001);
	Samurai::MessageHandler* mh = Samurai::MessageHandler::getInstance();

	mh->postMessage(0x51000001, nullptr, 7, 9);
	mh->process();

	return listener.seen == 1 && listener.last_arg1 == 7 && listener.last_arg2 == 9;
});

EXO_TEST(messagehandler_carries_the_data_pointer,
{
	int payload = 42;
	CountingListener listener(0x51000002);

	Samurai::MessageHandler::getInstance()->postMessage(0x51000002, &payload, 0, 0);
	Samurai::MessageHandler::getInstance()->process();

	return listener.seen == 1 && listener.last_data == &payload;
});

/* Every listener sees every message; EventMessage returning true does not
   stop delivery to the others. */
EXO_TEST(messagehandler_delivers_to_every_listener,
{
	CountingListener first(0x51000003);
	CountingListener second(0x51000003);

	Samurai::MessageHandler::getInstance()->postMessage(0x51000003, nullptr, 0, 0);
	Samurai::MessageHandler::getInstance()->process();

	return first.seen == 1 && second.seen == 1;
});

EXO_TEST(messagehandler_drains_the_queue,
{
	CountingListener listener(0x51000004);
	Samurai::MessageHandler* mh = Samurai::MessageHandler::getInstance();

	for (int n = 0; n < 5; n++) mh->postMessage(0x51000004, nullptr, 0, 0);
	mh->process();

	/* One pass takes all five, and a second pass finds nothing left. */
	const size_t after_first = listener.seen;
	mh->process();

	return after_first == 5 && listener.seen == 5;
});

/* A listener deregisters itself when it goes out of scope. */
EXO_TEST(messagehandler_stops_delivering_to_a_destroyed_listener,
{
	Samurai::MessageHandler* mh = Samurai::MessageHandler::getInstance();

	{
		CountingListener listener(0x51000005);
		mh->postMessage(0x51000005, nullptr, 0, 0);
		mh->process();
		if (listener.seen != 1) return false;
	}

	/* If the destroyed listener were still registered this would use freed
	   memory, which the sanitizer build would report. */
	mh->postMessage(0x51000005, nullptr, 0, 0);
	mh->process();
	return true;
});

EXO_TEST(messagehandler_remove_listener_stops_delivery,
{
	CountingListener listener(0x51000006);
	Samurai::MessageHandler* mh = Samurai::MessageHandler::getInstance();

	mh->removeMessageListener(&listener);
	mh->postMessage(0x51000006, nullptr, 0, 0);
	mh->process();

	const bool silent = listener.seen == 0;

	/* Put it back so the destructor's own remove has something to find. */
	mh->addMessageListener(&listener);
	return silent;
});

/*
 * The carry-over. postMessage() called while process() is draining the queue
 * puts the message on busy_queue, which is moved back to queue at the end of
 * the pass - so it is delivered by the *next* process(), never by the one that
 * is already running. Without that, the loop could feed itself forever.
 */
EXO_TEST(messagehandler_message_posted_during_process_waits_for_the_next_pass,
{
	RepostingListener listener(0x51000007, 0x51000008);
	Samurai::MessageHandler* mh = Samurai::MessageHandler::getInstance();

	mh->postMessage(0x51000007, nullptr, 0, 0);
	mh->process();

	/* The repost happened, but was not delivered within the same pass. */
	if (!listener.reposted || listener.saw_repost) return false;

	mh->process();
	return listener.saw_repost;
});

/* The standalone postMessage() reaches the same queue. */
EXO_TEST(messagehandler_standalone_post_reaches_listeners,
{
	CountingListener listener(0x51000009);

	Samurai::postMessage(0x51000009, nullptr, 1, 2);
	Samurai::MessageHandler::getInstance()->process();

	return listener.seen == 1 && listener.last_arg1 == 1 && listener.last_arg2 == 2;
});

/* ------------------------------------------------------------------------- */
/* Bandwidth accounting                                                      */
/*                                                                           */
/* getBps() deliberately ignores the second still filling, so a reading taken */
/* in the same second as the data is always zero - which means these need a   */
/* second boundary to cross, and the stale-window case needs the whole window */
/* to elapse.                                                                 */
/*                                                                            */
/* Waiting once per case cost the suite about eight seconds. All the readings */
/* are therefore taken by one fixture, built lazily and shared, which spends  */
/* the window once and lets every case assert against what it recorded.       */
/* ------------------------------------------------------------------------- */

namespace {

static void wait_for_the_next_second()
{
	const time_t start = time(nullptr);
	while (time(nullptr) == start)
		usleep(20 * 1000);
}

constexpr size_t bw_window = Samurai::Util::BANDWIDTH_ESTIMATION_TIMEOUT;

struct BandwidthReadings
{
	/* Taken in the same second as the data. */
	size_t estimator_same_second = 1;

	/* Taken one tick later. */
	size_t estimator_single = 0;
	size_t estimator_accumulated = 0;
	size_t manager_send = 0;
	size_t manager_recv = 0;
	size_t manager_mixed_send = 0;
	size_t manager_recv_only_send = 1;
	size_t manager_recv_only_recv = 0;

	/* Taken after the whole window has passed. */
	size_t estimator_after_window = 1;
};

static const BandwidthReadings& bandwidth_readings()
{
	static const BandwidthReadings readings = []
	{
		BandwidthReadings r;

		Samurai::Util::RateEstimator single;
		Samurai::Util::RateEstimator accumulated;
		Samurai::IO::Net::BandwidthManager both;
		Samurai::IO::Net::BandwidthManager mixed;
		Samurai::IO::Net::BandwidthManager recv_only;

		single.add(3000);
		accumulated.add(1000);
		accumulated.add(1000);
		accumulated.add(1000);
		both.dataSendTCP(6000);
		both.dataRecvTCP(3000);
		mixed.dataSendTCP(3000);
		mixed.dataSendUDP(3000);
		recv_only.dataRecvTCP(9000);

		r.estimator_same_second = single.getBps();

		wait_for_the_next_second();

		r.estimator_single = single.getBps();
		r.estimator_accumulated = accumulated.getBps();
		r.manager_send = both.getSendBps();
		r.manager_recv = both.getRecvBps();
		r.manager_mixed_send = mixed.getSendBps();
		r.manager_recv_only_send = recv_only.getSendBps();
		r.manager_recv_only_recv = recv_only.getRecvBps();

		for (size_t n = 0; n < bw_window; n++)
			wait_for_the_next_second();

		r.estimator_after_window = single.getBps();
		return r;
	}();
	return readings;
}

}

/* The second the data landed in is still filling, so it does not count yet. */
EXO_TEST(rate_estimator_ignores_the_second_still_filling,
{
	return bandwidth_readings().estimator_same_second == 0;
});

/* The rate is the average over the window, not the raw total. */
EXO_TEST(rate_estimator_reports_what_was_added_once_the_second_ticks,
{
	return bandwidth_readings().estimator_single == 3000 / bw_window;
});

EXO_TEST(rate_estimator_accumulates_within_one_second,
{
	return bandwidth_readings().estimator_accumulated == 3000 / bw_window;
});

/* Data older than the window is forgotten rather than counted forever. */
EXO_TEST(rate_estimator_forgets_a_stale_window,
{
	return bandwidth_readings().estimator_after_window == 0;
});

EXO_TEST(bandwidth_manager_counts_tcp_send_and_receive,
{
	return bandwidth_readings().manager_send == 6000 / bw_window
		&& bandwidth_readings().manager_recv == 3000 / bw_window;
});

/* UDP and TCP feed the same direction counter. */
EXO_TEST(bandwidth_manager_counts_udp_with_tcp,
{
	return bandwidth_readings().manager_mixed_send == 6000 / bw_window;
});

/* The two directions are accounted separately. */
EXO_TEST(bandwidth_manager_directions_are_independent,
{
	return bandwidth_readings().manager_recv_only_send == 0
		&& bandwidth_readings().manager_recv_only_recv == 9000 / bw_window;
});

/*
 * Case-insensitive comparison over views. These carry a length, so they are
 * used on ranges inside received buffers where nothing guarantees a terminator,
 * and the folding is ASCII only so that the same bytes compare the same way
 * whatever locale the process happens to be in.
 */
EXO_TEST(iequals_matches_regardless_of_case,
{
	return Samurai::Util::iequals("Password", "password")
		&& Samurai::Util::iequals("PASSWORD", "password")
		&& Samurai::Util::iequals("pAsSwOrD", "PaSsWoRd");
});

EXO_TEST(iequals_is_exact_on_length,
{
	return !Samurai::Util::iequals("password", "passwor")
		&& !Samurai::Util::iequals("passwor", "password");
});

EXO_TEST(iequals_two_empty_views_match,
{
	return Samurai::Util::iequals("", "")
		&& !Samurai::Util::iequals("", "a")
		&& !Samurai::Util::iequals("a", "");
});

/* No terminator anywhere: both sides are ranges inside longer buffers. */
EXO_TEST(iequals_does_not_read_past_the_view,
{
	const char haystack[] = "OPERATORxxxx";
	return Samurai::Util::iequals(std::string_view(haystack, 8), "operator")
		&& !Samurai::Util::iequals(std::string_view(haystack, 7), "operator");
});

/* A byte above 0x7f is left alone rather than folded through a negative index. */
EXO_TEST(iequals_leaves_high_bytes_alone,
{
	const char a[] = { (char) 0xc3, (char) 0x98, 0 };   /* UTF-8 'Ø' */
	const char b[] = { (char) 0xc3, (char) 0xb8, 0 };   /* UTF-8 'ø' */
	return Samurai::Util::iequals(a, a) && !Samurai::Util::iequals(a, b);
});

EXO_TEST(istarts_with_folds_case,
{
	return Samurai::Util::istarts_with("TTHL/root", "tthl/")
		&& Samurai::Util::istarts_with("anything", "")
		&& !Samurai::Util::istarts_with("tth", "tthl/")
		&& !Samurai::Util::istarts_with("xTTHL/", "tthl/");
});

EXO_TEST(ifind_reports_the_offset,
{
	return Samurai::Util::ifind("a Holiday Video.avi", "holiday") == 2
		&& Samurai::Util::ifind("a Holiday Video.avi", "VIDEO") == 10
		&& Samurai::Util::ifind("nothing here", "absent") == std::string_view::npos;
});

/* As strstr has it: everything contains the empty string, at the front. */
EXO_TEST(ifind_of_an_empty_needle_is_the_front,
{
	return Samurai::Util::ifind("abc", "") == 0
		&& Samurai::Util::ifind("", "") == 0
		&& Samurai::Util::ifind("", "a") == std::string_view::npos;
});

EXO_TEST(icontains_answers_the_same_question,
{
	return Samurai::Util::icontains("Concert Video.AVI", ".avi")
		&& !Samurai::Util::icontains("Concert Video.AVI", ".mp3");
});
