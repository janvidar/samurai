/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/thread/thread.h>

#ifdef SAMURAI_POSIX
#include <pthread.h>
#include <sys/resource.h>
#define USE_PTHREADS

#define THREAD_HANDLE(X)        pthread_t X

#endif

#ifdef SAMURAI_WINDOWS
#include <windows.h>
#define USE_WINTHREADS
typedef size_t useconds_t;

#define THREAD_HANDLE(X)        HANDLE X
#endif

#include <sys/time.h>

class ThreadPriv
{
	public:
		THREAD_HANDLE(handle);
};


Thread::Thread(size_t stackSize) : d(0)
{
	(void) stackSize;
	d = new ThreadPriv();
	d->handle = 0;
}


Thread::~Thread()
{
	terminate();
	delete d;
}


void Thread::terminate()
{
#ifdef USE_PTHREADS
	int rc = pthread_cancel(d->handle);
	if (rc)
	{
		QERR("Unable to cancel thread");
	}
	d->handle = 0;
#endif
}

void* Thread::startFunc(void* ptr)
{
#ifdef USE_PTHREADS	
	Thread* t = reinterpret_cast<Thread*>(ptr);
	t->run();
#endif
	return 0;
}

void Thread::start(Priority priority)
{
#ifdef USE_PTHREADS
	int rc = pthread_create(&d->handle, NULL, Thread::startFunc, this);
	if (rc == -1)
	{
		QERR("Unable to start thread!");
	}
#endif
	setPriority(priority);
}


void Thread::setPriority(Priority priority)
{
	int p;
	switch (priority)
	{
		case Priority_Idle:     p = 20;  break;
		case Priority_Lowest:   p = 10;  break;
		case Priority_Low:      p = 5;   break;
		case Priority_Normal:   p = 0;   break;
		case Priority_High:     p = -5;  break;
		case Priority_Highest:  p = -10; break;
		case Priority_Critical: p = -20; break;
	}
#ifdef USE_PTHREADS
	setpriority(PRIO_PROCESS, 0, p);
#endif
}


void Thread::wait()
{
#ifdef USE_PTHREADS
	pthread_join(d->handle, NULL);
#endif
}


