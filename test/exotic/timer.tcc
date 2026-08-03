/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/timer.h>
#include <samurai/io/net/socketmonitor.h>
#include <chrono>
#include <memory>

/*
 * Samurai::Timer had no coverage at all, and nothing drove TimerManager, so
 * every timer in the library was armed and dead - Socket's connect timeout
 * among them. What is asserted here is that a deadline fires, that the manager
 * survives a callback destroying a timer, and that SocketMonitor::wait() is
 * what drives both.
 *
 * All of it is deterministic and none of it touches the network. Elapsed-time
 * bounds are kept loose so a loaded machine does not fail them.
 */

namespace {

using Samurai::Timer;
using Samurai::TimerManager;
using Samurai::IO::Net::SocketMonitor;

class Counter : public Samurai::TimerListener
{
	public:
		size_t fired = 0;

		void EventTimeout(Timer*) override { fired++; }
};

/** A listener that destroys its own timer the first time it fires. */
class SelfDestroyer : public Samurai::TimerListener
{
	public:
		std::unique_ptr<Timer> timer;
		size_t fired = 0;

		void EventTimeout(Timer*) override
		{
			fired++;
			timer.reset();
		}
};

/** A listener that destroys a different timer when it fires. */
class OtherDestroyer : public Samurai::TimerListener
{
	public:
		std::unique_ptr<Timer> victim;
		size_t fired = 0;

		void EventTimeout(Timer*) override
		{
			fired++;
			victim.reset();
		}
};

/*
 * Pumping through the monitor rather than calling TimerManager::process()
 * directly: that wait() fires timers at all is the thing under test.
 */
template<typename Predicate>
bool timer_pump_until(Predicate done, size_t max_passes = 400, int ms = 5)
{
	SocketMonitor* monitor = SocketMonitor::getInstance();
	for (size_t n = 0; n < max_passes; n++)
	{
		if (done()) return true;
		monitor->wait(ms);
	}
	return done();
}

void timer_pump(size_t passes, int ms = 5)
{
	SocketMonitor* monitor = SocketMonitor::getInstance();
	for (size_t n = 0; n < passes; n++)
		monitor->wait(ms);
}

int elapsed_ms(Timer::clock::time_point since)
{
	return (int) std::chrono::duration_cast<std::chrono::milliseconds>(
		Timer::clock::now() - since).count();
}

}

EXO_TEST(timer_manager_has_an_instance,
{
	return TimerManager::getInstance() != nullptr;
});

EXO_TEST(timer_registers_itself_and_deregisters,
{
	const size_t baseline = TimerManager::getInstance()->size();
	{
		Counter counter;
		Timer timer(&counter, std::chrono::milliseconds(10000), true);
		if (TimerManager::getInstance()->size() != baseline + 1) return false;
	}
	return TimerManager::getInstance()->size() == baseline;
});

EXO_TEST(timer_time_to_next_is_negative_when_nothing_is_armed,
{
	/* Anything armed by an earlier case is gone: every timer here is scoped. */
	return TimerManager::getInstance()->timeToNext() < 0;
});

EXO_TEST(timer_time_to_next_is_bounded_by_the_nearest_deadline,
{
	Counter counter;
	Timer far(&counter, std::chrono::milliseconds(60000), true);
	Timer near(&counter, std::chrono::milliseconds(200), true);

	const int next = TimerManager::getInstance()->timeToNext();
	return next >= 0 && next <= 200;
});

EXO_TEST(timer_single_shot_fires_once,
{
	Counter counter;
	Timer timer(&counter, std::chrono::milliseconds(1), true);

	if (!timer_pump_until([&] { return counter.fired > 0; })) return false;

	/* Several more passes must not produce a second one. */
	timer_pump(20);
	return counter.fired == 1;
});

EXO_TEST(timer_repeating_fires_repeatedly,
{
	Counter counter;
	Timer timer(&counter, std::chrono::milliseconds(1), false);

	return timer_pump_until([&] { return counter.fired >= 3; });
});

EXO_TEST(timer_destroyed_before_it_is_due_never_fires,
{
	Counter counter;
	{
		Timer timer(&counter, std::chrono::milliseconds(1), true);
	}

	timer_pump(20);
	return counter.fired == 0;
});

/*
 * The case Socket::EventTimeout hits: a timer callback holds no reference
 * keeping anything alive, so the handler releasing the firing timer has to be
 * safe. TimerManager does its bookkeeping before the callback for exactly this.
 */
EXO_TEST(timer_destroyed_inside_its_own_callback_is_safe,
{
	SelfDestroyer listener;
	listener.timer = std::make_unique<Timer>(&listener, std::chrono::milliseconds(1), false);

	if (!timer_pump_until([&] { return listener.fired > 0; })) return false;

	/* Repeating, but released on its first firing, so it must not come back. */
	timer_pump(20);
	return listener.fired == 1 && !listener.timer;
});

/* The stale-heap-entry path: an entry surfacing for a timer that is gone must
   be skipped rather than followed. */
EXO_TEST(timer_that_destroys_another_timer_is_safe,
{
	OtherDestroyer listener;
	Counter victim_counter;

	listener.victim = std::make_unique<Timer>(&victim_counter,
	                                          std::chrono::milliseconds(50), true);
	Timer killer(&listener, std::chrono::milliseconds(1), true);

	if (!timer_pump_until([&] { return listener.fired > 0; })) return false;

	timer_pump(40);
	return victim_counter.fired == 0 && !listener.victim;
});

EXO_TEST(timer_reset_postpones_the_deadline,
{
	Counter counter;
	Timer timer(&counter, std::chrono::milliseconds(40), true);

	/* Keep pushing the deadline out; it must not fire while being reset. */
	for (int n = 0; n < 8; n++)
	{
		timer.reset();
		timer_pump(2, 2);
	}
	if (counter.fired != 0) return false;

	return timer_pump_until([&] { return counter.fired == 1; });
});

EXO_TEST(timer_instances_are_independent,
{
	Counter first, second;
	Timer quick(&first, std::chrono::milliseconds(1), true);
	Timer slow(&second, std::chrono::milliseconds(60000), true);

	if (!timer_pump_until([&] { return first.fired == 1; })) return false;
	return second.fired == 0;
});

EXO_TEST(timer_second_granularity_constructor_arms_a_deadline,
{
	Counter counter;
	Timer timer(&counter, (time_t) 30, true);

	const int next = TimerManager::getInstance()->timeToNext();
	return next > 1000 && counter.fired == 0;
});

/* ------------------------------------------------------------------------- */
/* SocketMonitor integration                                                 */
/*                                                                           */
/* wait() used to call neither process() nor timeToNext(), so a timer fired   */
/* only if the application happened to drive the manager itself - and nothing */
/* in the tree did.                                                          */
/* ------------------------------------------------------------------------- */

EXO_TEST(timer_monitor_wait_fires_a_due_timer,
{
	Counter counter;
	Timer timer(&counter, std::chrono::milliseconds(1), true);

	SocketMonitor::getInstance()->wait(20);
	return counter.fired == 1;
});

/* The poll has to be cut short by the nearer deadline, or a timer is late by
   however long the caller asked for - and callers ask for whole seconds. */
EXO_TEST(timer_monitor_wait_returns_before_its_timeout_when_a_timer_is_due,
{
	Counter counter;
	Timer timer(&counter, std::chrono::milliseconds(10), true);

	const Timer::clock::time_point start = Timer::clock::now();
	SocketMonitor::getInstance()->wait(5000);
	const int spent = elapsed_ms(start);

	return counter.fired == 1 && spent < 2000;
});

EXO_TEST(timer_monitor_wait_does_not_return_early_when_nothing_is_armed,
{
	if (TimerManager::getInstance()->timeToNext() >= 0) return false;

	const Timer::clock::time_point start = Timer::clock::now();
	SocketMonitor::getInstance()->wait(50);

	/* A loose lower bound: the point is that it waited, not how precisely. */
	return elapsed_ms(start) >= 20;
});

EXO_TEST(timer_monitor_wait_with_a_far_deadline_still_honours_its_timeout,
{
	Counter counter;
	Timer timer(&counter, std::chrono::milliseconds(60000), true);

	const Timer::clock::time_point start = Timer::clock::now();
	SocketMonitor::getInstance()->wait(30);

	return counter.fired == 0 && elapsed_ms(start) < 2000;
});
