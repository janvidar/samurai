/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/timer.h>
#include <samurai/timestamp.h>
#include <sys/time.h>
#include <time.h>

Samurai::Timer::Timer(Samurai::TimerListener* listener, time_t timeout_seconds, bool singleShot) : callback(listener), single_shot(singleShot), timeout(timeout_seconds)
{
	start = Samurai::TimeStamp();

	if (Samurai::TimerManager::getInstance())
		Samurai::TimerManager::getInstance()->add(this);
}

Samurai::Timer::~Timer()
{
	if (Samurai::TimerManager::getInstance())
		Samurai::TimerManager::getInstance()->remove(this);
}
		
void Samurai::Timer::internal_evaluate()
{
	Samurai::TimeStamp current;
	if (current > start + timeout)
		internal_timeout();
}

void Samurai::Timer::internal_timeout() {
	if (single_shot && Samurai::TimerManager::getInstance()) Samurai::TimerManager::getInstance()->remove(this);
	else start = Samurai::TimeStamp();
	callback->EventTimeout(this);
}

/*
void Samurai::Timer::reset(time_t timeout_seconds, bool single_shot) {

}


void Samurai::Timer::stop() {
	Samurai::TimerManager::getInstance()->remove(this);
}
*/

Samurai::TimerManager::TimerManager() : locked(false) {
}

Samurai::TimerManager::~TimerManager() {
}

// TODO: Optimize this somewhat.
void Samurai::TimerManager::process() {
	locked = true;
	std::vector<Samurai::Timer*>::iterator it;
	for (it = timers.begin(); it != timers.end(); it++) {
		(*it)->internal_evaluate();
	}
	
	// Add any timers that were added during lock.
	while (pending_add.size()) {
		Samurai::Timer* timer = pending_add.back();
		timers.push_back(timer);
		pending_add.pop_back();
	}
	
	// Remove any timers that were removed during lock.
	while (pending_remove.size()) {
		Samurai::Timer* timer = pending_remove.back();
		std::vector<Samurai::Timer*>::iterator it;
		for (it = timers.begin(); it != timers.end(); it++) {
			if (*it == timer) {
				timers.erase(it);
				break;
			}
		}
		pending_remove.pop_back();
	}
	
	locked = false;
}

void Samurai::TimerManager::add(Samurai::Timer* timer) {
	if (!locked) {
		timers.push_back(timer);
	} else {
		pending_add.push_back(timer);
	}
}

void Samurai::TimerManager::remove(Samurai::Timer* timer) {
	if (!locked) {
		std::vector<Samurai::Timer*>::iterator it;
		for (it = timers.begin(); it != timers.end(); it++) {
			if (*it == timer) {
				timers.erase(it);
				return;	
			}
		}
	}
	
	pending_remove.push_back(timer);
}

Samurai::TimerManager* Samurai::TimerManager::getInstance()
{
	static Samurai::TimerManager* manager = 0;
	if (!manager)
	{
		manager = new TimerManager();
	}
	return manager;
}

