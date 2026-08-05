/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/net/notifier.h>
#include <samurai/io/net/socketmonitor.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

/*
 * The notifier is how a thread that is not the loop gets the loop's attention.
 * What matters is that wait() returns because of the notification rather than
 * because its timeout expired, that the callback runs on the loop thread, and
 * that a burst of notifications is one wakeup rather than one each.
 */

namespace {

using Samurai::IO::Net::Notifier;

/* Pump until the predicate holds or the passes run out; returns the number of
   passes used, so a case can tell "woke immediately" from "waited". */
template<typename Predicate>
size_t pump_until(Predicate done, size_t max_passes = 200, int ms = 5)
{
	Samurai::IO::Net::SocketMonitor* monitor = Samurai::IO::Net::SocketMonitor::getInstance();
	size_t passes = 0;
	while (passes < max_passes && !done())
	{
		monitor->wait(ms);
		passes++;
	}
	return passes;
}

}

EXO_TEST(notifier_constructs_and_registers, {
	Samurai::IO::Net::SocketMonitor* monitor = Samurai::IO::Net::SocketMonitor::getInstance();
	const size_t before = monitor->size();

	std::shared_ptr<Notifier> notifier = Notifier::create(std::function<void()>());
	if (!notifier) return false;

	const bool registered = monitor->size() == before + 1
		&& notifier->getFD() != INVALID_SOCKET;

	notifier.reset();
	return registered && monitor->size() == before;
});

/* The callback belongs to the loop, whoever did the waking. */
EXO_TEST(notifier_runs_its_callback_on_the_loop_thread, {
	const std::thread::id loop_thread = std::this_thread::get_id();
	std::atomic<bool> fired{false};
	std::thread::id ran_on;

	std::shared_ptr<Notifier> notifier = Notifier::create(
		std::function<void()>([&]() {
			ran_on = std::this_thread::get_id();
			fired = true;
		}));
	if (!notifier) return false;

	std::thread waker([&]() { notifier->notify(); });
	pump_until([&]() { return fired.load(); });
	waker.join();

	const bool ok = fired.load() && ran_on == loop_thread;
	notifier.reset();
	return ok;
});

/*
 * The point of the socketpair: without it the loop would sit out its full
 * timeout. Waiting for a second with a notification already pending has to
 * return in far less than that.
 */
EXO_TEST(notifier_wakes_a_wait_that_would_otherwise_block, {
	std::atomic<bool> fired{false};

	std::shared_ptr<Notifier> notifier = Notifier::create(
		std::function<void()>([&]() { fired = true; }));
	if (!notifier) return false;

	notifier->notify();

	const auto start = std::chrono::steady_clock::now();
	Samurai::IO::Net::SocketMonitor::getInstance()->wait(1000);
	const auto elapsed = std::chrono::steady_clock::now() - start;

	const bool prompt =
		std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < 500;

	const bool ok = fired.load() && prompt;
	notifier.reset();
	return ok;
});

/*
 * Notifications coalesce. A callback that assumed one byte meant one item would
 * leave the rest of them sitting until something unrelated woke the loop again,
 * so what is asserted is that the count of callbacks is smaller than the count
 * of notifications, not equal to it.
 */
EXO_TEST(notifier_coalesces_a_burst_into_fewer_callbacks, {
	std::atomic<size_t> callbacks{0};

	std::shared_ptr<Notifier> notifier = Notifier::create(
		std::function<void()>([&]() { callbacks++; }));
	if (!notifier) return false;

	const size_t sent = 500;
	for (size_t n = 0; n < sent; n++)
		notifier->notify();

	pump_until([&]() { return callbacks.load() > 0; });

	/* Drain anything still queued so the count settles. */
	pump_until([]() { return false; }, 3);

	const bool ok = callbacks.load() > 0 && callbacks.load() < sent;
	notifier.reset();
	return ok;
});

/* Several threads waking at once is the normal case once workers exist. */
EXO_TEST(notifier_accepts_concurrent_wakers, {
	std::atomic<size_t> callbacks{0};

	std::shared_ptr<Notifier> notifier = Notifier::create(
		std::function<void()>([&]() { callbacks++; }));
	if (!notifier) return false;

	std::vector<std::thread> wakers;
	for (size_t n = 0; n < 4; n++)
		wakers.emplace_back([&]() {
			for (size_t i = 0; i < 50; i++) notifier->notify();
		});

	pump_until([&]() { return callbacks.load() > 0; });
	for (std::thread& waker : wakers) waker.join();
	pump_until([]() { return false; }, 3);

	const bool ok = callbacks.load() > 0;
	notifier.reset();
	return ok;
});

/* Nothing pending means no callback: a wakeup is never invented. */
EXO_TEST(notifier_stays_quiet_without_a_notification, {
	std::atomic<size_t> callbacks{0};

	std::shared_ptr<Notifier> notifier = Notifier::create(
		std::function<void()>([&]() { callbacks++; }));
	if (!notifier) return false;

	pump_until([]() { return false; }, 5);

	const bool ok = callbacks.load() == 0;
	notifier.reset();
	return ok;
});
