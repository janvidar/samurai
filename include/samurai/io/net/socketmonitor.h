/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_SOCKETMONITOR_H
#define HAVE_SAMURAI_SOCKETMONITOR_H

#include <chrono>
#include <map>
#include <memory>
#include <vector>
#include <samurai/samurai.h>
#include <samurai/io/net/socketglue.h>
#include <samurai/bitmask.h>
#include <string_view>

namespace Samurai {
namespace IO {
namespace Net {

class SocketBase;
class SocketMonitor;

/**
 * An interface for monitoring multiple sockets,
 * and reporting events.
 */
class SocketMonitor
{
	public:
		virtual ~SocketMonitor();

		/* The backends own descriptor arrays and epoll/kqueue descriptors,
		 * released in their destructors. */
		SocketMonitor(const SocketMonitor&) = delete;
		SocketMonitor& operator=(const SocketMonitor&) = delete;	
	
		/**
		 * Return the current socket monitor.
		 * If none is created yet, one will be created automatically.
		 * If one wish to create a custom socket monitor, it must
		 * be created and inserted with SetMonitor before getInstance()
		 * is called for the first time (at least to have a defined result).
		 */
		static SocketMonitor* getInstance();
		
		/**
		 * Set a custom, or specialized socket monitor.
		 * If one already is created, this will fail and return false.
		 */
		static bool setSocketMonitor(SocketMonitor* monitor);
		
		enum class Triggers : unsigned
		{
			None   = 0x00,
			Read   = 0x01,
			Write  = 0x02,
			Accept = 0x04, /**<< "Not used" */
			Close  = 0x08, /**<< "Not used" */
			Urgent = 0x10, /**<< "Not used -- urgent data to read" */
			Error  = 0x20, /**<< "Error" -- error */
		};

		/**
		 * Add a socket for monitoring. The trigger filter can be
		 * specified by ORing together values from enum Trigger.
		 */
		void add(SocketBase* socket);
		
		/**
		 * Remove a socket from the monitor.
		 * Pending signals will not be handled.
		 */
		void remove(SocketBase* socket);
		
		/**
		 * Update the socket and what should be monitored.
		 * This will replace the current trigger filter.
		 */
		void modify(SocketBase* socket);
	
		/**
		 * Wait for one or more events to occur, but not for longer than
		 * the given amount of milliseconds before returning.
		 * Events are triggered automatically.
		 *
		 * Armed timers are fired here as well, and the poll is cut short when
		 * one is due sooner than the given timeout. This is what drives
		 * Samurai::Timer, so a caller that never reaches this has no working
		 * timers - the connect timeout among them. It also means the timeout is
		 * an upper bound and not a sleep: a caller must not count passes to
		 * measure time.
		 *
		 * NOTE: readiness of the descriptor is not the whole story once TLS is
		 * involved. Reading one TLS record takes all of it off the descriptor
		 * and decrypts it, so a caller that asked for less than the record held
		 * leaves application data buffered in the TLS layer while the
		 * descriptor itself has nothing further to report - and no amount of
		 * polling will say so. Buffered data is therefore reported as readable
		 * in its own right, and the poll below does not block while any is
		 * outstanding.
		 */
		void wait(int time_ms);
		
		/**
		 * Returns the number of sockets currently being monitored.
		 */
		virtual size_t size() = 0;
		
		/**
		 * Returns the number of sockets this monitor is capable of handling.
		 * This depends much on the underlying engine and operating system
		 * limitations.
		 */
		virtual size_t capacity() = 0;
		
		/**
		 * Returns true if the socketmonitor can be used on the system or
		 * not. This allows for runtime configurable checks, or
		 * checks for bugs in the system calls of the system.
		 */
		virtual bool isValid() = 0;

	protected:
		/**
		 * Add a socket for monitoring. The trigger filter can be
		 * specified by ORing together values from enum Trigger.
		 */
		virtual void internal_add(SocketBase* socket) = 0;
		
		/**
		 * Remove a socket from the monitor.
		 * Pending signals will not be handled.
		 */
		virtual void internal_remove(SocketBase* socket) = 0;
		
		/**
		 * Update the socket and what should be monitored.
		 * This will replace the current trigger filter.
		 */
		virtual void internal_modify(SocketBase* socket) = 0;

		/**
		 * Poll the descriptors and dispatch what the operating system reports,
		 * waiting no longer than the given number of milliseconds.
		 */
		virtual void internal_wait(int time_ms) = 0;

		/**
		 * The default handler for the socket events.
		 * This one will tell each socket what to do based on their status.
		 *
		 * @param trig see Triggers (ORed)
		 */
		virtual void handleSocketEvent(SocketBase* socket, Triggers trig);

		/**
		 * Deliver an event for the socket registered on the given descriptor.
		 *
		 * Backends call this rather than handleSocketEvent() directly, and key
		 * their bookkeeping on the descriptor rather than on a SocketBase*: a
		 * raw pointer collected before dispatch is worthless once a handler
		 * has released the socket it belongs to. The weak reference recorded
		 * here is locked for the duration of the callback, so a socket that
		 * its owner drops mid-dispatch stays alive until the callback returns,
		 * and one that was already released is skipped instead of followed.
		 */
		void dispatch(socket_t fd, Triggers trig);

	private:
		/** Whether any monitored socket holds buffered input right now. */
		bool haveBufferedInput() const;

		/** Deliver a read event to every socket that holds buffered input. */
		void dispatchBufferedInput();

		/**
		 * What is known about one monitored descriptor.
		 *
		 * Everything past the reference is the runaway guard. Every backend here
		 * is level triggered, so a descriptor that is ready stays ready until
		 * something acts on it: bytes are taken off it, it is closed, or the
		 * interest in it changes. A handler that answers the report by doing none
		 * of those is reported again on the next pass, and on every pass after
		 * that, which is a busy loop at the speed of the poll rather than an
		 * event that got lost. Enough of those in a row and the monitor stops
		 * believing the descriptor; see dispatch().
		 */
		struct Watch
		{
			std::weak_ptr<SocketBase> socket;

			/**
			 * Consecutive readiness reports answered with nothing at all.
			 *
			 * What each report is measured against is read either side of the
			 * callback rather than kept here on purpose: a socket written to from
			 * somewhere other than its own handler - a timer, say - is still not
			 * answering the reports it is being given, and remembering the figure
			 * between passes would let that count as an answer.
			 */
			unsigned idle = 0;

			/**
			 * When a descriptor taken out of the poll goes back in, or the epoch
			 * for one that was never taken out. Not the same thing as being
			 * unmonitored: the socket still believes it is watched, its registry
			 * entry is still here, and its owner is never told.
			 */
			bool paused = false;
			std::chrono::steady_clock::time_point resume{};

			/**
			 * Whether this has been complained about already.
			 *
			 * A descriptor that never starts answering is paused again every time
			 * it is let back in, which is four times a second - and a log line at
			 * that rate is how the one line that mattered gets lost. Cleared by
			 * progress, so a second episode is reported as a second episode.
			 */
			bool reported = false;
		};

		/**
		 * Take a descriptor out of the poll for a while, having concluded that
		 * nothing is answering it. Says so once, loudly, because a spin that is
		 * merely slowed down is still a bug somewhere above this.
		 */
		void pauseWatch(socket_t fd, Watch& watch, SocketBase* socket, Triggers trig);

		/** Put back whatever has waited long enough, and say when the next is due. */
		int resumePaused();

	protected:
		explicit SocketMonitor(std::string_view name);
		static SocketMonitor* socket_monitor;
		std::string_view name;

		std::map<socket_t, Watch> registry;
};

/* SocketMonitor::Triggers is a flag set; see samurai/bitmask.h. */
SAMURAI_DECLARE_BITMASK(SocketMonitor::Triggers)

}
}
}

#endif // HAVE_SAMURAI_SOCKETMONITOR_H
