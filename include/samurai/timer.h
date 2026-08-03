/*
 * Copyright (C) 2001-2006 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_TIMER_H
#define HAVE_SAMURAI_TIMER_H

#include <stdint.h>
#include <time.h>

#include <chrono>
#include <unordered_set>
#include <vector>

namespace Samurai {

	class Timer;

class TimerListener {
	public:
		virtual ~TimerListener() { }

		/**
		 * Implement this to receive the timer events.
		 * @param timer pointer to the timer that fired.
		 */
		virtual void EventTimeout(Timer* timer) = 0;
};

/**
 * NOTE: timing is on std::chrono::steady_clock rather than wall-clock
 * TimeStamps: a wall-clock jump - NTP stepping the clock, or the operator
 * changing the timezone - must not move a deadline with it.
 */
class Timer {

	public:
		typedef std::chrono::steady_clock clock;

		/** Second granularity, kept for source compatibility. */
		Timer(TimerListener* listener, time_t timeout_seconds, bool single_shot);

		/** Sub-second granularity. */
		Timer(TimerListener* listener, std::chrono::milliseconds timeout, bool single_shot);

		~Timer();

		/** When this timer is next due. */
		clock::time_point deadline() const { return due; }

		/** Restart the interval from now. */
		void reset();

	protected:
		void internal_fire(clock::time_point now);

	public:
		/* Registers itself with the TimerManager, which keys on the instance,
		 * so a copy would appear in the manager twice. */
		Timer(const Timer&) = delete;
		Timer& operator=(const Timer&) = delete;

		TimerListener* callback;
		bool single_shot;
		std::chrono::milliseconds interval;
		clock::time_point due;

		/*
		 * Distinguishes this timer from any other that has ever existed,
		 * including one the allocator later places at this same address. The
		 * manager's heap refers to timers by pointer, so without this a stale
		 * entry left behind by a destroyed timer would appear to belong to
		 * whatever was allocated in its place.
		 */
		uint64_t serial;

		friend class TimerManager;
};

/**
 * Don't interface with this class directly,
 * constructing a Timer, or destructing it will automatically
 * handle registrations.
 */
class TimerManager {
	public:
		TimerManager();
		~TimerManager();

		static TimerManager* getInstance();

	public:
		/** Fire every timer whose deadline has passed. */
		void process();

		/**
		 * Milliseconds until the next deadline, or -1 if no timer is armed.
		 * Suitable as a socket monitor timeout, so an idle loop can block
		 * until there is something to do rather than spinning on a fixed tick.
		 */
		int timeToNext() const;

		size_t size() const { return live.size(); }

	protected:
		void add(Timer* timer);
		void remove(Timer* timer);
		void schedule(Timer* timer);

		/*
		 * NOTE: the heap is ordered by deadline and holds entries, not timers:
		 * an entry whose timer has been removed is skipped when it surfaces,
		 * which avoids having to erase from the middle of a heap. 'live' is the
		 * authority on what still exists, so a callback that destroys another
		 * timer cannot leave a dangling pointer behind.
		 */
		struct Entry {
			Timer::clock::time_point due;
			Timer* timer;
			/* Which timer this entry was made for, so an address the allocator
			   has since handed to a different timer cannot be mistaken for it. */
			uint64_t serial;
			/* Greater-than, so push_heap/pop_heap give a min-heap by deadline. */
			bool operator<(const Entry& other) const { return due > other.due; }
		};

		/** True when 'e' still refers to the timer it was made for. */
		static bool current(const Entry& e, const std::unordered_set<Timer*>& alive);

		std::vector<Entry> heap;
		std::unordered_set<Timer*> live;

	friend class Timer;
};


}

#endif // HAVE_SAMURAI_TIMER_H
