/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_EVENTLOOP_H
#define HAVE_SAMURAI_EVENTLOOP_H

#include <memory>
#include <unordered_set>

namespace Samurai {

class TimerManager;
class MessageHandler;

namespace IO {
namespace Net {
class SocketMonitor;
class SocketBase;
class BandwidthManager;
namespace DNS { class ResolverPool; }
}
}

/**
 * One event loop: the sockets it polls, the timers it fires, the messages posted
 * to it, the lookups it is told the answers to, and what it counts.
 *
 * Everything that waits on one of those is made with the loop it belongs to and
 * keeps to it, so two loops in one process share nothing - which is what lets two
 * cores, or two test environments, exist side by side. A loop is used from one
 * thread, and outlives what is made on it; one that is destroyed first detaches
 * the sockets, timers and listeners still registered, so their own destructors
 * find nothing to do.
 */
class EventLoop {
	public:
		/** With the best socket monitor this platform has. */
		EventLoop();

		/** With the monitor given, which the loop then owns; see isValid(). */
		explicit EventLoop(std::unique_ptr<IO::Net::SocketMonitor> monitor);

		~EventLoop();

		EventLoop(const EventLoop&) = delete;
		EventLoop& operator=(const EventLoop&) = delete;

		/** False when no socket monitor could be made; nothing will be polled. */
		bool isValid() const;

		IO::Net::SocketMonitor& monitor();
		TimerManager& timers();
		MessageHandler& messages();
		IO::Net::BandwidthManager& bandwidth();

		/** Started on first use, since it runs threads of its own. */
		IO::Net::DNS::ResolverPool& resolver();

		/**
		 * One turn: wait up to 'time_ms' for a socket (less when a timer is due
		 * sooner), dispatch it, fire what is due, deliver what was posted.
		 */
		void wait(int time_ms);

	private:
		/* Every socket made on this loop and not yet destroyed, to detach if the loop goes first. */
		std::unordered_set<IO::Net::SocketBase*> sockets;
		void join(IO::Net::SocketBase* socket) { sockets.insert(socket); }
		void leave(IO::Net::SocketBase* socket) { sockets.erase(socket); }
		friend class IO::Net::SocketBase;

		/* In this order, so they go in the reverse one: the resolver's notifier
		   is a socket, and every socket leaves the monitor on its way out. */
		std::unique_ptr<IO::Net::SocketMonitor> poll;
		std::unique_ptr<TimerManager> timerManager;
		std::unique_ptr<IO::Net::BandwidthManager> counters;
		std::unique_ptr<MessageHandler> messageHandler;
		std::unique_ptr<IO::Net::DNS::ResolverPool> pool;
};

}

#endif // HAVE_SAMURAI_EVENTLOOP_H
