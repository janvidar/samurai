/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_SOCKETMONITOR_H
#define HAVE_SAMURAI_SOCKETMONITOR_H

#include <map>
#include <memory>
#include <vector>
#include <samurai/samurai.h>
#include <samurai/io/net/socketglue.h>

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
		
		enum Triggers
		{
			MNone   = 0x00,
			MRead   = 0x01,
			MWrite  = 0x02,
			MAccept = 0x04, /**<< "Not used" */
			MClose  = 0x08, /**<< "Not used" */
			MUrgent = 0x10, /**<< "Not used -- urgent data to read" */
			MError  = 0x20, /**<< "Error" -- error */
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
		 * NOTE: readiness of the descriptor is not the whole story once TLS is
		 * involved. Reading one TLS record takes all of it off the descriptor
		 * and decrypts it, so a caller that asked for less than the record held
		 * leaves application data buffered in the TLS layer while the
		 * descriptor itself has nothing further to report - and no amount of
		 * polling will say so. This used to strand that data until the peer
		 * happened to send something else, which for a request/response
		 * protocol means never: each side waits for the other. Buffered data is
		 * therefore reported as readable in its own right, and the poll below
		 * does not block while any is outstanding.
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
		 * @param trig see enum Triggers (ORed)
		 */
		virtual void handleSocketEvent(SocketBase* socket, int trig);

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
		void dispatch(socket_t fd, int trig);

	private:
		/** Whether any monitored socket holds buffered input right now. */
		bool haveBufferedInput() const;

		/** Deliver a read event to every socket that holds buffered input. */
		void dispatchBufferedInput();

	protected:
		SocketMonitor(const char* name);
		static SocketMonitor* socket_monitor;
		const char* name;

		std::map<socket_t, std::weak_ptr<SocketBase> > registry;
};

}
}
}

#endif // HAVE_SAMURAI_SOCKETMONITOR_H
