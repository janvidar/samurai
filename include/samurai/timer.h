/*
 * Copyright (C) 2001-2006 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_QUICKDC_TIMER_H
#define HAVE_QUICKDC_TIMER_H

#include <time.h>
#include <vector>

#include <samurai/timestamp.h>

namespace Samurai {
	
	class TimeStamp;
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

class Timer {

	public:
		Timer(TimerListener* listener, time_t timeout_seconds, bool single_shot);
		virtual ~Timer();
		
/*
		void reset(time_t timeout_seconds, bool single_shot);
		void stop();
*/

	protected:
		void internal_evaluate();
		void internal_timeout();
		
	private:
		TimerListener* callback;
		bool single_shot;
		Samurai::TimeStamp start;
		time_t timeout;
		
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
		void process();
		
	protected:
		void add(Timer* timer);
		void remove(Timer* timer);
		
		std::vector<Timer*> timers;
		std::vector<Timer*> pending_remove;
		std::vector<Timer*> pending_add;
		
		Samurai::TimeStamp next;
		bool locked;
		
	friend class Timer;
};


}

#endif // HAVE_QUICKDC_TIMER_H
