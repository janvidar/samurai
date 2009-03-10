/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_SOCKETMONITOR_H
#define HAVE_SAMURAI_SOCKETMONITOR_H

#include <vector>
#include <samurai/samurai.h>

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
		 */
		 virtual void wait(int time_ms) = 0;
		
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
		 * The default handler for the socket events.
		 * This one will tell each socket what to do based on their status.
		 *
		 * @param trig see enum Triggers (ORed)
		 */
		virtual void handleSocketEvent(SocketBase* socket, int trig);
	
	protected:
		SocketMonitor(const char* name);
		static SocketMonitor* socket_monitor;
		const char* name;
};

}
}
}

#endif // HAVE_SAMURAI_SOCKETMONITOR_H
