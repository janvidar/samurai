/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/net/inetaddress.h>

#include "io/net/socketmonitor-backend.h"

#include <sys/socket.h>
#include <unistd.h>
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

struct Pair
{
	int fd[2] = { -1, -1 };
	Pair() { if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) fd[0] = fd[1] = -1; }
	bool valid() const { return fd[0] != -1 && fd[1] != -1; }

	Pair(const Pair&) = delete;
	Pair& operator=(const Pair&) = delete;
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
	::close(pair.fd[1]);
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
	::close(pair.fd[1]);
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
	::close(pair.fd[1]);
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
	::close(pair.fd[1]);
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
	::close(pair.fd[1]);
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
	::close(pair.fd[1]);
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
	::close(pair.fd[1]);
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
	::close(pair.fd[1]);
	return ok;
});
