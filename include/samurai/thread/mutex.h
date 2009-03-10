/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_QUICKDC_MUTEX_H
#define HAVE_QUICKDC_MUTEX_H


class MutexPriv;

class Mutex
{
	public:
		Mutex();
		~Mutex();
		
	public:
		bool tryLock();
		void lock();
		void unlock();
	
	private:
		MutexPriv* d;
};
 
class MutexLocker
{
	public:
		MutexLocker(Mutex& pm);
		~MutexLocker();
		
	private:
		Mutex& m;
};


#endif // HAVE_QUICKDC_MUTEX_H

