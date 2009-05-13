/*
 * Copyright (C) 2009 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_UPNP_WORKER_H
#define HAVE_UPNP_WORKER_H

#include "upnp.h"

#include <samurai/samurai.h>
#include <samurai/io/buffer.h>
#include <samurai/io/net/datagram.h>
#include <samurai/io/net/multicast.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/net/serversocket.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/net/hardwareaddress.h>
#include <samurai/io/net/interface.h>


namespace UPnP
{
	class Packet;

	class Worker
		: public Samurai::IO::Net::DatagramEventHandler
	{
		public:
			// FIXME: iface should be const and non-pointer
			Worker(Samurai::IO::Net::NetworkInterface* iface, const Samurai::IO::Net::InetSocketAddress& address);
			virtual ~Worker();

			bool isOK() const { return m_socket != 0; }
			void send(UPnP::Packet& packet);

			void locateServices();
		
		protected:

			void EventGotDatagram(Samurai::IO::Net::DatagramSocket*, Samurai::IO::Net::DatagramPacket* packet);
			void EventDatagramError(const Samurai::IO::Net::DatagramSocket*, const char*);

		private:
			Samurai::IO::Net::MulticastSocket* m_socket;
			Samurai::IO::Net::InetSocketAddress m_addr;
	};

} // namespace UPnP

#endif // HAVE_UPNP_WORKER_H