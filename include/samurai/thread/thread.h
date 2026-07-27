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
		void setPriority(enum Priority); /* no-op */
		void start(enum Priority = Priority_Normal);
		void terminate();
		virtual void run() = 0;

	protected:
		static void* startFunc(void*);

	private:
		Thread(const Thread&);
		Thread& operator=(const Thread&);

		ThreadPriv* d;
};

#endif // HAVE_QUICKDC_THREAD_H

