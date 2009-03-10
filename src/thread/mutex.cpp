/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/thread/mutex.h>

#ifdef SAMURAI_POSIX
#include <pthread.h>
#define MTX_DECL(X)       pthread_mutex_t X
#define MTX_INIT(X)       pthread_mutex_init(X, 0)
#define MTX_FINI(X)       pthread_mutex_destroy(X)
#define MTX_LOCK(X)       pthread_mutex_lock(X)
#define MTX_TRY_LOCK(X)   pthread_mutex_trylock(X)
#define MTX_UNLOCK(X)     pthread_mutex_unlock(X)
#endif

#ifdef SAMURAI_WINDOWS
#include <windows.h>
#define MTX_DECL(X)       CRITICAL_SECTION X
#define MTX_INIT(X)       InitializeCriticalSection(X)
#define MTX_FINI(X)       DeleteCriticalSection(X)
#define MTX_LOCK(X)       EnterCriticalSection(X)
#define MTX_TRY_LOCK(X)   TryEnterCriticalSection(X)
#define MTX_UNLOCK(X)     LeaveCriticalSection(X)
#endif

class MutexPriv
{
	public:
		MTX_DECL(m);
};

Mutex::Mutex() : d(0)
{
	d = new MutexPriv();
	MTX_INIT(&d->m);
}

Mutex::~Mutex()
{
	MTX_FINI(&d->m);
}

bool Mutex::tryLock()
{
	return MTX_TRY_LOCK(&d->m);
}

void Mutex::lock()
{
	MTX_LOCK(&d->m);
}

void Mutex::unlock()
{
	MTX_UNLOCK(&d->m);
}

MutexLocker::MutexLocker(Mutex& pm) : m(pm)
{
	m.lock();
}

MutexLocker::~MutexLocker()
{
	m.unlock();
}
