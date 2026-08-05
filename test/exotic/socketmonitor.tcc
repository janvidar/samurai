/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/net/inetaddress.h>

#include "io/net/socketmonitor-backend.h"

#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <memory>
#include <vector>

/*
 * createDefaultMonitor() picks epoll on Linux and kqueue on the BSDs, so the
 * poll and select backends are compiled into the library and shipped without
 * ever executing. select's stale-descriptor recovery in particular is the most
 * delicate logic in the group and had never been run.
 *
 * A backend is constructed directly here rather than through getInstance().
 * add(), remove(), modify() and wait() are public and the registry is
 * per-instance, so a backend can be driven completely without touching the
 * singleton - which matters because sockets.tcc is one long stateful scenario
 * over it.
 */

using Triggers = Samurai::IO::Net::SocketMonitor::Triggers;

namespace {

/*
 * A socket that records what it was told rather than acting on it. SocketBase's
 * descriptor-adopting constructor is public, so this needs none of Socket's
 * state machine or event handler.
 */
class ProbeSocket final : public Samurai::IO::Net::SocketBase
{
	public:
		static std::shared_ptr<ProbeSocket> create(socket_t fd,
			const Samurai::IO::Net::SocketAddress& addr)
		{
			return std::shared_ptr<ProbeSocket>(new ProbeSocket(fd, addr));
		}

		Triggers last = Triggers::None;
		int events = 0;

		void handleMonitorEvent(Triggers trig) override
		{
			last = trig;
			events++;
		}

		/* monitor_trigger is protected, and setMonitor() would route through
		   the singleton, which is not the monitor under test. */
		void setTrigger(Triggers trig) { monitor_trigger = trig; }

	private:
		ProbeSocket(socket_t fd, const Samurai::IO::Net::SocketAddress& addr)
			: Samurai::IO::Net::SocketBase(fd, addr) { }
};

/*
 * Owns both ends. SocketBase's destructor deregisters from the monitor but does
 * not close the descriptor, so a ProbeSocket wrapping one of these does not take
 * it over - which makes closing exactly once, here, the only safe arrangement.
 * Closing twice would be worse than untidy: between the two, something else can
 * be handed the same number and lose it.
 */
struct Pair
{
	int fd[2] = { -1, -1 };

	Pair() { if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) fd[0] = fd[1] = -1; }

	~Pair()
	{
		for (int n = 0; n < 2; n++) close_end(n);
	}

	void close_end(int n)
	{
		if (fd[n] == -1) return;
		::close(fd[n]);
		fd[n] = -1;
	}

	bool valid() const { return fd[0] != -1 && fd[1] != -1; }

	Pair(const Pair&) = delete;
	Pair& operator=(const Pair&) = delete;
};

/* A descriptor at or above FD_SETSIZE, obtained without opening a thousand
   files. Closed here because ProbeSocket does not own what it wraps. */
struct HighDescriptor
{
	int fd = -1;

	explicit HighDescriptor(int from) { fd = fcntl(from, F_DUPFD, FD_SETSIZE); }
	~HighDescriptor() { if (fd != -1) ::close(fd); }

	bool obtained() const { return fd >= FD_SETSIZE; }
	bool refused() const  { return fd < 0; }

	HighDescriptor(const HighDescriptor&) = delete;
	HighDescriptor& operator=(const HighDescriptor&) = delete;
};

static Samurai::IO::Net::InetSocketAddress probe_address()
{
	Samurai::IO::Net::InetAddress addr("127.0.0.1");
	return Samurai::IO::Net::InetSocketAddress(addr, 0);
}

/* SocketBase adopts the descriptor and closes it, so the pair must not. */
static std::shared_ptr<ProbeSocket> adopt(int fd, Triggers trig)
{
	std::shared_ptr<ProbeSocket> probe = ProbeSocket::create((socket_t) fd, probe_address());
	probe->setTrigger(trig);
	return probe;
}

}

/* ------------------------------------------------------------------------- */
/* Construction                                                              */
/* ------------------------------------------------------------------------- */

EXO_TEST(monitor_select_constructs_valid,
{
	Samurai::IO::Net::SelectSocketMonitor monitor;
	return monitor.isValid() && monitor.size() == 0 && monitor.capacity() > 0;
});

EXO_TEST(monitor_poll_constructs_valid,
{
	Samurai::IO::Net::PollSocketMonitor monitor;
	return monitor.isValid() && monitor.size() == 0;
});

/* Waiting on an empty set must return rather than block for the full timeout
   forever, and must dispatch nothing. */
EXO_TEST(monitor_select_wait_on_empty_set,
{
	Samurai::IO::Net::SelectSocketMonitor monitor;
	monitor.wait(0);
	return monitor.size() == 0;
});

EXO_TEST(monitor_poll_wait_on_empty_set,
{
	Samurai::IO::Net::PollSocketMonitor monitor;
	monitor.wait(0);
	return monitor.size() == 0;
});

/* ------------------------------------------------------------------------- */
/* Registration                                                              */
/* ------------------------------------------------------------------------- */

EXO_TEST(monitor_select_add_and_remove,
{
	Pair pair;
	if (!pair.valid()) return false;

	Samurai::IO::Net::SelectSocketMonitor monitor;
	std::shared_ptr<ProbeSocket> a = adopt(pair.fd[0], Triggers::Read);
	std::shared_ptr<ProbeSocket> b = adopt(pair.fd[1], Triggers::Read);

	monitor.add(a.get());
	monitor.add(b.get());
	if (monitor.size() != 2) return false;

	monitor.remove(a.get());
	if (monitor.size() != 1) return false;

	monitor.remove(b.get());
	return monitor.size() == 0;
});

EXO_TEST(monitor_poll_add_and_remove,
{
	Pair pair;
	if (!pair.valid()) return false;

	Samurai::IO::Net::PollSocketMonitor monitor;
	std::shared_ptr<ProbeSocket> a = adopt(pair.fd[0], Triggers::Read);
	std::shared_ptr<ProbeSocket> b = adopt(pair.fd[1], Triggers::Read);

	monitor.add(a.get());
	monitor.add(b.get());
	if (monitor.size() != 2) return false;

	monitor.remove(a.get());
	monitor.remove(b.get());
	return monitor.size() == 0;
});

/* Removing something never added must not disturb the set. */
EXO_TEST(monitor_select_remove_unknown_is_harmless,
{
	Pair pair;
	if (!pair.valid()) return false;

	Samurai::IO::Net::SelectSocketMonitor monitor;
	std::shared_ptr<ProbeSocket> a = adopt(pair.fd[0], Triggers::Read);
	std::shared_ptr<ProbeSocket> b = adopt(pair.fd[1], Triggers::Read);

	monitor.add(a.get());
	monitor.remove(b.get());
	return monitor.size() == 1;
});

/* A socket with no valid descriptor is refused rather than registered. */
EXO_TEST(monitor_rejects_invalid_descriptor,
{
	Samurai::IO::Net::SelectSocketMonitor monitor;
	monitor.add(nullptr);
	return monitor.size() == 0;
});

/* ------------------------------------------------------------------------- */
/* Readiness                                                                 */
/* ------------------------------------------------------------------------- */

EXO_TEST(monitor_select_reports_readable,
{
	Pair pair;
	if (!pair.valid()) return false;

	Samurai::IO::Net::SelectSocketMonitor monitor;
	std::shared_ptr<ProbeSocket> reader = adopt(pair.fd[0], Triggers::Read);
	monitor.add(reader.get());

	if (::write(pair.fd[1], "x", 1) != 1) return false;

	monitor.wait(500);

	const bool ok = reader->events > 0 && any(reader->last & Triggers::Read);
	monitor.remove(reader.get());
	return ok;
});

EXO_TEST(monitor_poll_reports_readable,
{
	Pair pair;
	if (!pair.valid()) return false;

	Samurai::IO::Net::PollSocketMonitor monitor;
	std::shared_ptr<ProbeSocket> reader = adopt(pair.fd[0], Triggers::Read);
	monitor.add(reader.get());

	if (::write(pair.fd[1], "x", 1) != 1) return false;

	monitor.wait(500);

	const bool ok = reader->events > 0 && any(reader->last & Triggers::Read);
	monitor.remove(reader.get());
	return ok;
});

/* An idle descriptor must not be reported as ready. */
EXO_TEST(monitor_select_quiet_socket_is_not_reported,
{
	Pair pair;
	if (!pair.valid()) return false;

	Samurai::IO::Net::SelectSocketMonitor monitor;
	std::shared_ptr<ProbeSocket> reader = adopt(pair.fd[0], Triggers::Read);
	monitor.add(reader.get());

	monitor.wait(0);

	const bool ok = reader->events == 0;
	monitor.remove(reader.get());
	return ok;
});

/* A socketpair end is writable immediately, so the Write trigger fires. */
EXO_TEST(monitor_select_reports_writable,
{
	Pair pair;
	if (!pair.valid()) return false;

	Samurai::IO::Net::SelectSocketMonitor monitor;
	std::shared_ptr<ProbeSocket> writer = adopt(pair.fd[0], Triggers::Write);
	monitor.add(writer.get());

	monitor.wait(500);

	const bool ok = writer->events > 0 && any(writer->last & Triggers::Write);
	monitor.remove(writer.get());
	return ok;
});

EXO_TEST(monitor_poll_reports_writable,
{
	Pair pair;
	if (!pair.valid()) return false;

	Samurai::IO::Net::PollSocketMonitor monitor;
	std::shared_ptr<ProbeSocket> writer = adopt(pair.fd[0], Triggers::Write);
	monitor.add(writer.get());

	monitor.wait(500);

	const bool ok = writer->events > 0 && any(writer->last & Triggers::Write);
	monitor.remove(writer.get());
	return ok;
});

/* A removed socket must stop being reported even though it is still ready. */
EXO_TEST(monitor_select_removed_socket_is_not_dispatched,
{
	Pair pair;
	if (!pair.valid()) return false;

	Samurai::IO::Net::SelectSocketMonitor monitor;
	std::shared_ptr<ProbeSocket> reader = adopt(pair.fd[0], Triggers::Read);
	monitor.add(reader.get());
	if (::write(pair.fd[1], "x", 1) != 1) return false;

	monitor.remove(reader.get());
	monitor.wait(0);

	const bool ok = reader->events == 0;
	return ok;
});

/* modify() changes what is watched without re-registering. */
EXO_TEST(monitor_select_modify_changes_the_trigger,
{
	Pair pair;
	if (!pair.valid()) return false;

	Samurai::IO::Net::SelectSocketMonitor monitor;
	std::shared_ptr<ProbeSocket> probe = adopt(pair.fd[0], Triggers::Read);
	monitor.add(probe.get());

	/* Nothing to read, so Read alone reports nothing. */
	monitor.wait(0);
	if (probe->events != 0) { monitor.remove(probe.get()); ::close(pair.fd[1]); return false; }

	probe->setTrigger(Triggers::Write);
	monitor.modify(probe.get());
	monitor.wait(500);

	const bool ok = probe->events > 0 && any(probe->last & Triggers::Write);
	monitor.remove(probe.get());
	return ok;
});

/*
 * select()'s stale-descriptor recovery. Closing a descriptor behind the
 * monitor's back makes select() fail with EBADF for the whole set; rather than
 * spinning on it, the backend identifies the unusable descriptors and reports
 * them to their owners as errors so they can be torn down.
 */
EXO_TEST(monitor_select_recovers_from_a_stale_descriptor,
{
	Pair pair;
	if (!pair.valid()) return false;

	Samurai::IO::Net::SelectSocketMonitor monitor;
	std::shared_ptr<ProbeSocket> probe = adopt(pair.fd[0], Triggers::Read);
	monitor.add(probe.get());

	/* Closed without telling the monitor, which is the situation the recovery
	   path exists for. */
	::close(pair.fd[0]);

	monitor.wait(0);

	const bool ok = probe->events > 0 && any(probe->last & Triggers::Error);

	monitor.remove(probe.get());
	return ok;
});

/*
 * select()'s FD_SETSIZE bound.
 *
 * fd_set is a fixed size bitmap and FD_SET() on a descriptor at or above
 * FD_SETSIZE writes past the end of it - past three fd_sets on internal_wait()'s
 * own stack frame. A process whose descriptor limit is higher can be handed such
 * a socket while monitoring only a handful, so the backend has to leave it out.
 *
 * Reaching the bound needs a descriptor numbered at least FD_SETSIZE, which
 * F_DUPFD provides directly - it duplicates onto the lowest free descriptor at
 * or above the number given, so this costs one call rather than a thousand open
 * files.
 */
EXO_TEST(monitor_select_skips_a_descriptor_past_fd_setsize,
{
	Pair pair;
	if (!pair.valid()) return false;

	HighDescriptor high(pair.fd[0]);
	if (high.refused()) return true;   /* the limit will not allow it here */
	if (!high.obtained()) return false;

	Samurai::IO::Net::SelectSocketMonitor monitor;
	std::shared_ptr<ProbeSocket> probe = adopt(high.fd, Triggers::Read);
	monitor.add(probe.get());

	/* Readable, so it would be reported if it were in the set at all. */
	const bool wrote = ::write(pair.fd[1], "x", 1) == 1;

	monitor.wait(0);

	const bool skipped = probe->events == 0;
	monitor.remove(probe.get());
	return wrote && skipped;
});

/* The same descriptor is reported perfectly well by a backend without the
   bound, which is what makes the case above a limitation and not a defect. */
EXO_TEST(monitor_poll_handles_a_descriptor_past_fd_setsize,
{
	Pair pair;
	if (!pair.valid()) return false;

	HighDescriptor high(pair.fd[0]);
	if (high.refused()) return true;
	if (!high.obtained()) return false;

	Samurai::IO::Net::PollSocketMonitor monitor;
	std::shared_ptr<ProbeSocket> probe = adopt(high.fd, Triggers::Read);
	monitor.add(probe.get());

	if (::write(pair.fd[1], "x", 1) != 1) return false;

	monitor.wait(500);

	const bool reported = probe->events > 0 && any(probe->last & Triggers::Read);
	monitor.remove(probe.get());
	return reported;
});

/* A skipped descriptor must not stop the ones that do fit from being served. */
EXO_TEST(monitor_select_still_serves_low_descriptors_alongside_a_skipped_one,
{
	Pair low;
	Pair high_pair;
	if (!low.valid() || !high_pair.valid()) return false;

	HighDescriptor high(high_pair.fd[0]);
	if (high.refused()) return true;

	Samurai::IO::Net::SelectSocketMonitor monitor;
	std::shared_ptr<ProbeSocket> over = adopt(high.fd, Triggers::Read);
	std::shared_ptr<ProbeSocket> under = adopt(low.fd[0], Triggers::Read);
	monitor.add(over.get());
	monitor.add(under.get());

	if (::write(low.fd[1], "x", 1) != 1) return false;
	if (::write(high_pair.fd[1], "x", 1) != 1) return false;

	monitor.wait(500);

	const bool ok = under->events > 0 && over->events == 0;
	monitor.remove(over.get());
	monitor.remove(under.get());
	return ok;
});
