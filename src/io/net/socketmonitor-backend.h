/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_SOCKETMONITOR_BACKEND_H
#define HAVE_SAMURAI_SOCKETMONITOR_BACKEND_H

#include <samurai/io/net/socketmonitor.h>

#if defined(SAMURAI_OS_LINUX)
#define SOCKET_NOTIFY_EPOLL
struct epoll_event;
#endif

#if defined(SAMURAI_BSD)
#define SOCKET_NOTIFY_KQUEUE
struct kevent;
#endif

#if defined(SAMURAI_UNIX)
#define SOCKET_NOTIFY_POLL
struct pollfd;
#endif

// Use as fallback for all platforms.
#define SOCKET_NOTIFY_SELECT

struct poll_act {
	Samurai::IO::Net::SocketBase* sock;
	int trig;
};

namespace Samurai {
namespace IO {
namespace Net {

class SocketBase;
class SocketMonitor;

#ifdef SOCKET_NOTIFY_POLL
class PollSocketMonitor : public SocketMonitor
{
	public:
		PollSocketMonitor();
		virtual ~PollSocketMonitor();
		
		void internal_add(SocketBase* socket);
		void internal_remove(SocketBase* socket);
		void internal_modify(SocketBase* socket);
		void wait(int time_ms);
		size_t size() { return num; }
		size_t capacity() { return max; }
		bool isValid();
		
	private:
		struct pollfd* list;
		struct poll_act* act;
		SocketBase** sockets;
		size_t num;
		size_t max;
};
#endif // SOCKET_NOTIFY_POLL

#ifdef SOCKET_NOTIFY_EPOLL
class EPollSocketMonitor : public SocketMonitor
{
	public:
		EPollSocketMonitor();
		virtual ~EPollSocketMonitor();
		
		void internal_add(SocketBase* socket);
		void internal_remove(SocketBase* socket);
		void internal_modify(SocketBase* socket);
		
		void wait(int time_ms);
		size_t size() { return num; }
		size_t capacity() { return max; }
		bool isValid();
		
	private:
		void debug();
		struct epoll_event* act;
		int epfd;
		size_t num;
		size_t max;
};
#endif // SOCKET_NOTIFY_EPOLL

#ifdef SOCKET_NOTIFY_KQUEUE
class KQueueSocketMonitor : public SocketMonitor
{
	public:
		KQueueSocketMonitor();
		virtual ~KQueueSocketMonitor();
		
		void internal_add(SocketBase* socket);
		void internal_remove(SocketBase* socket);
		void internal_modify(SocketBase* socket);
		void wait(int time_ms);
		size_t size() { return num; }
		size_t capacity() { return max; }
		bool isValid();
	
	protected:
		struct kevent* getChangeEventSlot();
	
	private:
		std::vector<SocketBase*> sockets;
		struct kevent* events;
		struct kevent* change;
		int kfd;
		size_t num;
		size_t max;
		size_t numChanges;
};
#endif // SOCKET_NOTIFY_KQUEUE


#ifdef SOCKET_NOTIFY_SELECT
class SelectSocketMonitor : public SocketMonitor
{
	public:
		SelectSocketMonitor();
		virtual ~SelectSocketMonitor();
		
		void internal_add(SocketBase* socket);
		void internal_remove(SocketBase* socket);
		void internal_modify(SocketBase* socket);
		void wait(int time_ms);
		size_t size() { return sockets.size(); }
		size_t capacity() { return max; }
		bool isValid();
		
	private:
		std::vector<SocketBase*> sockets;
		struct poll_act* act;
		size_t max;
};
#endif // SOCKET_NOTIFY_SELECT


}
}
}

#endif // HAVE_SAMURAI_SOCKETMONITOR_BACKEND_H

