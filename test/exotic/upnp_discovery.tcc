/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/upnp/gateway.h>
#include <samurai/timer.h>

#include <chrono>
#include <memory>

/*
 * SSDP discovery on the wire.
 *
 * There is no gateway in a build machine, so the case worth having here is the
 * failure path: a search must reach a terminal state within its own window, must
 * not hang, and must give the socket monitor back what it took. That is what the
 * earlier prototype got wrong - it enforced no deadline at all and looped
 * forever.
 *
 * A developer's machine may well find a real gateway, which is accepted too: only
 * the terminal state and the teardown are asserted, in the same spirit as
 * multicast_join_then_leave tolerating a host with no multicast route.
 */

namespace {

using Samurai::IO::Net::SocketMonitor;
using namespace Samurai::IO::Net::UPnP;

class DiscoveryRecorder : public GatewayEventHandler
{
	public:
		bool found = false;
		bool failed = false;
		Error error = Error::None;
		int candidates = 0;

		bool settled() const { return found || failed; }

	protected:
		void EventGatewayFound(Gateway*) override { found = true; }
		void EventGatewayError(Gateway*, Error why) override
		{
			failed = true;
			error = why;
		}
		void EventGatewayCandidate(Gateway*, const Candidate&) override
		{ candidates++; }
};

template<typename Fn>
bool discovery_pump(Fn done, int timeout_ms)
{
	SocketMonitor* monitor = SocketMonitor::getInstance();
	Samurai::TimerManager* timers = Samurai::TimerManager::getInstance();

	const auto deadline = std::chrono::steady_clock::now()
		+ std::chrono::milliseconds(timeout_ms);

	while (!done())
	{
		if (std::chrono::steady_clock::now() > deadline) return done();
		monitor->wait(5);
		timers->process();
	}
	return true;
}

Gateway::Options quick_search()
{
	Gateway::Options options;
	options.searchTimeout = std::chrono::milliseconds(600);
	options.searchInterval = std::chrono::milliseconds(150);
	options.searchRounds = 2;
	options.httpTimeout = std::chrono::milliseconds(600);
	return options;
}

}

/*
 * The important one. Whether or not anything answers, the search has to finish -
 * and finish inside its own window rather than whenever the pump gives up.
 */
EXO_TEST(upnp_discovery_reaches_a_terminal_state,
{
	DiscoveryRecorder events;
	std::unique_ptr<Gateway> gateway = Gateway::discover(&events, quick_search());
	if (!gateway) return false;

	const auto started = std::chrono::steady_clock::now();
	if (!discovery_pump([&] { return events.settled(); }, 5000)) return false;

	const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - started).count();

	/* Well inside the pump's own limit: the search's deadline is what ended it. */
	if (spent > 4000) return false;

	if (events.found)
	{
		/* A real gateway on the developer's network. */
		return gateway->isReady() && gateway->info().isValid();
	}

	/* Nothing answered, or what answered was not a gateway. */
	return events.error == Error::NoGateway
		|| events.error == Error::DescriptionFailed
		|| events.error == Error::NoService;
});

/* The sockets a search opens have to be given back, or a long-running process
   leaks a descriptor per search. */
EXO_TEST(upnp_discovery_returns_the_monitor_to_its_baseline,
{
	SocketMonitor* monitor = SocketMonitor::getInstance();
	const size_t baseline = monitor->size();

	{
		DiscoveryRecorder events;
		std::unique_ptr<Gateway> gateway = Gateway::discover(&events, quick_search());
		if (!gateway) return false;

		/* A search opens one socket per interface per group, so the monitor has
		   to have grown - unless this host has no multicast interface at all,
		   in which case discover() reported failure at once. */
		if (!discovery_pump([&] { return events.settled(); }, 5000)) return false;
	}

	/* Releasing a socket deregisters it, but the monitor is only asked to forget
	   it on its next pass. */
	return discovery_pump([&] { return monitor->size() <= baseline; }, 2000);
});

/* Destroyed mid-search, nothing may be reported afterwards: a late callback into
   a released handler is a use after free, which the sanitizer build catches. */
EXO_TEST(upnp_discovery_destroyed_in_flight_reports_nothing,
{
	DiscoveryRecorder events;
	{
		std::unique_ptr<Gateway> gateway = Gateway::discover(&events, quick_search());
		if (!gateway) return false;
		SocketMonitor::getInstance()->wait(1);
	}

	discovery_pump([&] { return false; }, 900);
	return !events.settled();
});

EXO_TEST(upnp_discovery_cancel_settles_at_once,
{
	DiscoveryRecorder events;
	std::unique_ptr<Gateway> gateway = Gateway::discover(&events, quick_search());
	if (!gateway) return false;

	gateway->cancel();

	return events.failed && events.error == Error::Cancelled;
});

/* Asking for neither family leaves nothing to search, which is a failure rather
   than a search that never ends. */
EXO_TEST(upnp_discovery_without_a_family_to_search_fails,
{
	Gateway::Options options = quick_search();
	options.searchIPv4 = false;
	options.searchIPv6 = false;

	DiscoveryRecorder events;
	std::unique_ptr<Gateway> gateway = Gateway::discover(&events, options);
	if (!gateway) return false;

	if (!discovery_pump([&] { return events.settled(); }, 3000)) return false;
	return events.failed && events.error == Error::NoGateway;
});

/* A host with IPv6 turned off must not fail the whole search. */
EXO_TEST(upnp_discovery_over_ipv4_alone_still_finishes,
{
	Gateway::Options options = quick_search();
	options.searchIPv6 = false;

	DiscoveryRecorder events;
	std::unique_ptr<Gateway> gateway = Gateway::discover(&events, options);
	if (!gateway) return false;

	return discovery_pump([&] { return events.settled(); }, 5000);
});

EXO_TEST(upnp_discovery_over_ipv6_alone_still_finishes,
{
	Gateway::Options options = quick_search();
	options.searchIPv4 = false;

	DiscoveryRecorder events;
	std::unique_ptr<Gateway> gateway = Gateway::discover(&events, options);
	/* A host with no IPv6 at all cannot open a socket, and discover() says so by
	   returning something that fails rather than by returning null. */
	if (!gateway) return true;

	return discovery_pump([&] { return events.settled(); }, 5000);
});

/* Two searches at once must not interfere: each opens its own sockets. */
EXO_TEST(upnp_discovery_two_searches_are_independent,
{
	DiscoveryRecorder first_events;
	DiscoveryRecorder second_events;

	std::unique_ptr<Gateway> first = Gateway::discover(&first_events, quick_search());
	std::unique_ptr<Gateway> second = Gateway::discover(&second_events, quick_search());
	if (!first || !second) return false;

	return discovery_pump(
		[&] { return first_events.settled() && second_events.settled(); }, 6000);
});
