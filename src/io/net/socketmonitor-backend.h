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

/* Keyed by descriptor rather than by SocketBase*: a pointer gathered before
   dispatch is worthless once a handler has released its socket. */
struct poll_act {
	socket_t fd;
	int trig;
};

namespace Samurai {
namespace IO {
namespace Net {

class SocketBase;
class SocketMonitor;

#ifdef SOCKET_NOTIFY_POLL
class PollSocketMonitor final : public SocketMonitor
{
	public:
		PollSocketMonitor();
		~PollSocketMonitor() override;
		
		void internal_add(SocketBase* socket) override;
		void internal_remove(SocketBase* socket) override;
		void internal_modify(SocketBase* socket) override;
		void internal_wait(int time_ms) override;
		size_t size() override { return num; }
		size_t capacity() override { return max; }
		bool isValid() override;
		
	private:
		struct pollfd* list;
		struct poll_act* act;
		SocketBase** sockets;
		size_t num;
		size_t max;
};
#endif // SOCKET_NOTIFY_POLL

#ifdef SOCKET_NOTIFY_EPOLL
class EPollSocketMonitor final : public SocketMonitor
{
	public:
		EPollSocketMonitor();
		~EPollSocketMonitor() override;
		
		void internal_add(SocketBase* socket) override;
		void internal_remove(SocketBase* socket) override;
		void internal_modify(SocketBase* socket) override;
		
		void internal_wait(int time_ms) override;
		size_t size() override { return num; }
		size_t capacity() override { return max; }
		bool isValid() override;
		
	private:
		void debug();
		struct epoll_event* act;
		int epfd;
		size_t num;
		size_t max;
};
#endif // SOCKET_NOTIFY_EPOLL

#ifdef SOCKET_NOTIFY_KQUEUE
class KQueueSocketMonitor final : public SocketMonitor
{
	public:
		KQueueSocketMonitor();
		~KQueueSocketMonitor() override;
		
		void internal_add(SocketBase* socket) override;
		void internal_remove(SocketBase* socket) override;
		void internal_modify(SocketBase* socket) override;
		void internal_wait(int time_ms) override;
		size_t size() override { return num; }
		size_t capacity() override { return max; }
		bool isValid() override;
	
	protected:
		struct kevent* getChangeEventSlot();
		void internal_set(SocketBase* socket);

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
class SelectSocketMonitor final : public SocketMonitor
{
	public:
		SelectSocketMonitor();
		~SelectSocketMonitor() override;
		
		void internal_add(SocketBase* socket) override;
		void internal_remove(SocketBase* socket) override;
		void internal_modify(SocketBase* socket) override;
		void internal_wait(int time_ms) override;
		size_t size() override { return sockets.size(); }
		size_t capacity() override { return max; }
		bool isValid() override;
		
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

