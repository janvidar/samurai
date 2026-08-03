/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/timer.h>

#include <algorithm>
#include <chrono>

Samurai::Timer::Timer(Samurai::TimerListener* listener, time_t timeout_seconds, bool singleShot)
	: callback(listener)
	, single_shot(singleShot)
	, interval(std::chrono::seconds(timeout_seconds))
	, due(clock::now() + std::chrono::seconds(timeout_seconds))
{
	if (Samurai::TimerManager::getInstance())
		Samurai::TimerManager::getInstance()->add(this);
}


Samurai::Timer::Timer(Samurai::TimerListener* listener, std::chrono::milliseconds timeout, bool singleShot)
	: callback(listener)
	, single_shot(singleShot)
	, interval(timeout)
	, due(clock::now() + timeout)
{
	if (Samurai::TimerManager::getInstance())
		Samurai::TimerManager::getInstance()->add(this);
}


Samurai::Timer::~Timer()
{
	if (Samurai::TimerManager::getInstance())
		Samurai::TimerManager::getInstance()->remove(this);
}


void Samurai::Timer::reset()
{
	due = clock::now() + interval;

	if (Samurai::TimerManager::getInstance())
		Samurai::TimerManager::getInstance()->schedule(this);
}


void Samurai::Timer::internal_fire(clock::time_point now)
{
	/*
	 * Bookkeeping before the callback: the handler is entitled to delete this
	 * timer, so nothing may touch a member once it has been called.
	 */
	if (single_shot)
	{
		if (Samurai::TimerManager::getInstance())
			Samurai::TimerManager::getInstance()->remove(this);
	}
	else
	{
		due = now + interval;
		if (Samurai::TimerManager::getInstance())
			Samurai::TimerManager::getInstance()->schedule(this);
	}

	if (callback) callback->EventTimeout(this);
}


Samurai::TimerManager::TimerManager()
{
}


Samurai::TimerManager::~TimerManager()
{
}


void Samurai::TimerManager::add(Samurai::Timer* timer)
{
	if (!timer) return;

	live.insert(timer);
	schedule(timer);
}


void Samurai::TimerManager::remove(Samurai::Timer* timer)
{
	/*
	 * Only the authoritative set is updated. Heap entries naming this timer
	 * are left where they are and skipped when they surface, which is what
	 * makes it safe for a callback to destroy a timer that process() has not
	 * reached yet.
	 */
	live.erase(timer);
}


void Samurai::TimerManager::schedule(Samurai::Timer* timer)
{
	if (!timer) return;

	Entry e;
	e.due = timer->due;
	e.timer = timer;

	heap.push_back(e);
	std::push_heap(heap.begin(), heap.end());
}


void Samurai::TimerManager::process()
{
	const Timer::clock::time_point now = Timer::clock::now();

	while (!heap.empty() && heap.front().due <= now)
	{
		std::pop_heap(heap.begin(), heap.end());
		const Entry e = heap.back();
		heap.pop_back();

		/* Stale in one of two ways: the timer was destroyed, or it was
		   rescheduled and this is the superseded entry. */
		if (live.find(e.timer) == live.end()) continue;
		if (e.timer->due != e.due) continue;

		e.timer->internal_fire(now);
	}
}


int Samurai::TimerManager::timeToNext() const
{
	const Timer::clock::time_point now = Timer::clock::now();
	bool found = false;
	Timer::clock::time_point best;

	/* Only the root of a heap is ordered, and entries may be stale, so the
	   nearest live deadline is found by inspection. */
	for (size_t n = 0; n < heap.size(); n++)
	{
		const Entry& e = heap[n];
		if (live.find(e.timer) == live.end()) continue;
		if (e.timer->due != e.due) continue;

		if (e.due <= now) return 0;

		if (!found || e.due < best)
		{
			best = e.due;
			found = true;
		}
	}

	if (!found) return -1;

	/*
	 * Rounded up, because this is a poll timeout and the deadline has to have
	 * passed by the time the poll returns. Truncating gives a wait that is
	 * fractionally too short, so the caller finds nothing due and polls again -
	 * spinning for the remainder of the millisecond rather than sleeping
	 * through it.
	 */
	return (int) std::chrono::ceil<std::chrono::milliseconds>(best - now).count();
}


Samurai::TimerManager* Samurai::TimerManager::getInstance()
{
	/* Function-local static: initialised once, thread-safely, and without the
	   check-then-assign race the raw pointer version had - which also never
	   freed the manager. */
	static Samurai::TimerManager manager;
	return &manager;
}
