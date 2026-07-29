/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/net/dns/resolver.h>
#include <samurai/io/net/dns/resolver-pool.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/socketmonitor.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

/*
 * Lookups run on worker threads and are reported on the loop thread. Every case
 * here pumps the monitor rather than sleeping, so what is asserted is that the
 * result arrives through the loop - which is the whole point of the pool - and
 * not merely that it arrives.
 *
 * Only 'localhost' and address literals are resolved: a case that needed a real
 * name server would fail wherever there is not one.
 */

namespace {

using Samurai::IO::Net::DNS::Resolver;
using Samurai::IO::Net::DNS::ResolverPool;

class Recorder : public Samurai::IO::Net::ResolveEventHandler
{
	public:
		size_t found = 0;
		size_t errors = 0;
		Resolver::Error last_error = Resolver::Error::Unknown;
		std::string address;
		std::thread::id ran_on;

		void EventHostFound(const Samurai::IO::Net::InetAddress* addr) override
		{
			ran_on = std::this_thread::get_id();
			address = addr ? addr->toString() : "";
			found++;
		}

		void EventHostError(Resolver::Error error) override
		{
			ran_on = std::this_thread::get_id();
			last_error = error;
			errors++;
		}
};

template<typename Predicate>
void resolver_pump_until(Predicate done, size_t max_passes = 400, int ms = 5)
{
	Samurai::IO::Net::SocketMonitor* monitor = Samurai::IO::Net::SocketMonitor::getInstance();
	for (size_t n = 0; n < max_passes && !done(); n++)
		monitor->wait(ms);
}

void resolver_pump(size_t passes, int ms = 5)
{
	Samurai::IO::Net::SocketMonitor* monitor = Samurai::IO::Net::SocketMonitor::getInstance();
	for (size_t n = 0; n < passes; n++)
		monitor->wait(ms);
}

/* Let workers retire quickly so the scale-down cases do not wait out the
   production timeout. */
void useShortIdleTimeout()
{
	ResolverPool::getInstance()->setIdleTimeout(std::chrono::milliseconds(100));
}

}

EXO_TEST(resolver_pool_resolves_localhost, {
	useShortIdleTimeout();
	Recorder recorder;

	std::unique_ptr<Resolver> resolver = Resolver::getHostByName(&recorder, "localhost");
	if (!resolver) return false;

	resolver_pump_until([&]() { return recorder.found || recorder.errors; });

	return recorder.found == 1 && recorder.errors == 0 && !recorder.address.empty();
});

/* The result belongs to the loop, whichever worker produced it. */
EXO_TEST(resolver_pool_reports_on_the_loop_thread, {
	Recorder recorder;
	const std::thread::id loop_thread = std::this_thread::get_id();

	std::unique_ptr<Resolver> resolver = Resolver::getHostByName(&recorder, "localhost");
	if (!resolver) return false;

	resolver_pump_until([&]() { return recorder.found || recorder.errors; });

	return recorder.found == 1 && recorder.ran_on == loop_thread;
});

/* .invalid is reserved by RFC 2606 precisely so that it never resolves. */
EXO_TEST(resolver_pool_reports_a_name_that_does_not_resolve, {
	Recorder recorder;

	std::unique_ptr<Resolver> resolver =
		Resolver::getHostByName(&recorder, "samurai-no-such-host.invalid");
	if (!resolver) return false;

	resolver_pump_until([&]() { return recorder.found || recorder.errors; });

	return recorder.errors == 1 && recorder.found == 0;
});

EXO_TEST(resolver_pool_rejects_an_empty_name, {
	Recorder recorder;

	std::unique_ptr<Resolver> resolver = Resolver::getHostByName(&recorder, "");
	if (!resolver) return false;

	/* Refused outright, without troubling a worker. */
	return recorder.errors == 1 && recorder.last_error == Resolver::Error::NotFound;
});

/*
 * Destroying the resolver is what cancels. Nothing may call back afterwards -
 * the handler is a local, so a late call would be a use-after-free that ASan
 * would report.
 */
EXO_TEST(resolver_pool_cancelled_before_completion_never_reports, {
	Recorder recorder;

	{
		std::unique_ptr<Resolver> resolver = Resolver::getHostByName(&recorder, "localhost");
		if (!resolver) return false;
	}

	resolver_pump(40);

	return recorder.found == 0 && recorder.errors == 0;
});

/* Several cancelled at once, including some still queued behind others. */
EXO_TEST(resolver_pool_cancels_queued_requests_too, {
	Recorder recorder;

	{
		std::vector<std::unique_ptr<Resolver>> resolvers;
		for (size_t n = 0; n < 16; n++)
			resolvers.push_back(Resolver::getHostByName(&recorder, "localhost"));
	}

	resolver_pump(40);

	return recorder.found == 0 && recorder.errors == 0;
});

/* A live lookup alongside a cancelled one still arrives. */
EXO_TEST(resolver_pool_cancelling_one_leaves_the_others, {
	Recorder kept;
	Recorder dropped;

	std::unique_ptr<Resolver> live = Resolver::getHostByName(&kept, "localhost");
	{
		std::unique_ptr<Resolver> doomed = Resolver::getHostByName(&dropped, "localhost");
	}

	resolver_pump_until([&]() { return kept.found || kept.errors; });

	return kept.found == 1 && dropped.found == 0 && dropped.errors == 0;
});

EXO_TEST(resolver_pool_handles_several_lookups_at_once, {
	const size_t count = 12;
	std::vector<std::unique_ptr<Recorder>> recorders;
	std::vector<std::unique_ptr<Resolver>> resolvers;

	for (size_t n = 0; n < count; n++)
	{
		recorders.push_back(std::make_unique<Recorder>());
		resolvers.push_back(Resolver::getHostByName(recorders.back().get(), "localhost"));
	}

	resolver_pump_until([&]() {
		for (const std::unique_ptr<Recorder>& r : recorders)
			if (!r->found && !r->errors) return false;
		return true;
	});

	for (const std::unique_ptr<Recorder>& r : recorders)
		if (r->found != 1) return false;
	return true;
});

/* The cap is a cap, however much arrives at once. */
EXO_TEST(resolver_pool_never_exceeds_its_worker_limit, {
	ResolverPool* pool = ResolverPool::getInstance();
	pool->setMaxWorkers(4);

	std::vector<std::unique_ptr<Recorder>> recorders;
	std::vector<std::unique_ptr<Resolver>> resolvers;

	size_t peak = 0;
	for (size_t n = 0; n < 32; n++)
	{
		recorders.push_back(std::make_unique<Recorder>());
		resolvers.push_back(Resolver::getHostByName(recorders.back().get(), "localhost"));
		if (pool->getWorkerCount() > peak) peak = pool->getWorkerCount();
	}

	resolver_pump_until([&]() {
		for (const std::unique_ptr<Recorder>& r : recorders)
			if (!r->found && !r->errors) return false;
		return true;
	});

	if (pool->getWorkerCount() > peak) peak = pool->getWorkerCount();
	return peak <= 4;
});

/* Idle workers give their threads back, and a later lookup brings one along. */
EXO_TEST(resolver_pool_retires_idle_workers_and_starts_them_again, {
	ResolverPool* pool = ResolverPool::getInstance();
	pool->setIdleTimeout(std::chrono::milliseconds(100));

	{
		Recorder recorder;
		std::unique_ptr<Resolver> resolver = Resolver::getHostByName(&recorder, "localhost");
		resolver_pump_until([&]() { return recorder.found || recorder.errors; });
	}

	resolver_pump_until([&]() { return pool->getWorkerCount() == 0; }, 200, 10);
	if (pool->getWorkerCount() != 0) return false;

	Recorder again;
	std::unique_ptr<Resolver> resolver = Resolver::getHostByName(&again, "localhost");
	resolver_pump_until([&]() { return again.found || again.errors; });

	return again.found == 1;
});

/* An address literal is already resolved, so it never reaches the pool. */
EXO_TEST(resolver_pool_is_not_used_for_an_address_literal, {
	ResolverPool* pool = ResolverPool::getInstance();
	resolver_pump_until([&]() { return pool->getWorkerCount() == 0; }, 200, 10);

	Samurai::IO::Net::InetAddress addr("127.0.0.1");
	const bool resolved = addr.isResolved();

	return resolved && pool->getWorkerCount() == 0;
});
