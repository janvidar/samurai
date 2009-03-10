/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_QUICKDC_THREAD_H
#define HAVE_QUICKDC_THREAD_H

#include <samurai/samurai.h>

#ifdef SAMURAI_WINDOWS
#define useconds_t size_t
#endif

class ThreadPriv;

class Thread
{
	public:
		enum Priority {
			Priority_Idle,
			Priority_Lowest,
			Priority_Low,
			Priority_Normal,
			Priority_High,
			Priority_Highest,
			Priority_Critical,
		};

		enum State
		{
			State_New,
			State_Running,
			State_Waiting, /* possible? */
			State_Terminated
		};

	public:
		Thread(size_t stackSize);
		virtual ~Thread();
		
		void wait();
		void setPriority(enum Priority);
		enum Priority getPriority() const;
		void start(enum Priority = Priority_Normal);
		void terminate();
		bool running() const;
		bool finished() const;
		virtual void run() = 0;
		
	protected:
		static Thread* currentThread();
		static void exit();
		static void sleep(time_t seconds);
		static void msleep(time_t msecs);
		static void usleep(useconds_t usecs);
		static void add(Thread* thread);
		static void remove(Thread* thread);
		static std::vector<Thread*> threads;
		
		static void* startFunc(void*);
		
	private:
		ThreadPriv* d;
};

#endif // HAVE_QUICKDC_THREAD_H

