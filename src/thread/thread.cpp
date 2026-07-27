/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/thread/thread.h>

#include <string.h>

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
		ThreadPriv() : started(false) { }

		THREAD_HANDLE(handle);
		bool started;
};


Thread::Thread(size_t stackSize) : d(0)
{
	(void) stackSize;
	d = new ThreadPriv();
}


Thread::~Thread()
{
	terminate();
	delete d;
}


void Thread::terminate()
{
#ifdef USE_PTHREADS
	if (!d->started)
		return;

	int rc = pthread_cancel(d->handle);
	if (rc)
	{
		QERR("Unable to cancel thread: %s", strerror(rc));
	}
	d->started = false;
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
	if (d->started)
	{
		QERR("Thread is already running");
		return;
	}

	int rc = pthread_create(&d->handle, NULL, Thread::startFunc, this);
	if (rc != 0)
	{
		QERR("Unable to start thread: %s", strerror(rc));
		return;
	}

	d->started = true;
#endif
	setPriority(priority);
}


void Thread::setPriority(Priority priority)
{
	(void) priority; /* no-op: setpriority() renices the process, not the thread */
}


void Thread::wait()
{
#ifdef USE_PTHREADS
	if (!d->started)
		return;

	int rc = pthread_join(d->handle, NULL);
	if (rc != 0)
	{
		QERR("Unable to join thread: %s", strerror(rc));
		return;
	}

	d->started = false;
#endif
}


