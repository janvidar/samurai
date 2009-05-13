/*
 * Copyright (C) 2001-2009 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <vector>
#include <stdlib.h>

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

#include "upnp.h"
#include "upnpworker.h"

class UPnPHandler
	: public Samurai::IO::Net::ServerSocketEventHandler
{
	public:
		UPnPHandler()
			: m_address(Samurai::IO::Net::InetAddress("239.255.255.250"), 1900)
		{
			setupWorkers();
		}
		
		virtual ~UPnPHandler()
		{
			while (m_workers.size())
			{
				UPnP::Worker* worker = m_workers.back();
				m_workers.pop_back();
				delete worker;
			}
		}
		
		void browseServices()
		{
			if (!m_workers.size())
				return;
		
			for (std::vector<UPnP::Worker*>::iterator it = m_workers.begin(); it != m_workers.end(); it++)
			{
				(*it)->locateServices();
			}
		}
	protected:

		
		void EventAcceptError(const Samurai::IO::Net::ServerSocket*, const char* /*msg*/)
		{
			puts("Accept error");
		}
		
		void EventAcceptSocket(const Samurai::IO::Net::ServerSocket*, Samurai::IO::Net::Socket* socket)
		{
			puts("Accepted socket");
			(void) socket;
		}

	private:
		void setupWorkers()
		{
			std::vector<Samurai::IO::Net::NetworkInterface*> interfaces;
			bool ok = Samurai::IO::Net::NetworkInterface::getInterfaces(interfaces);
			if (!ok)
			{
				QERR("Unable to detect networking devices");
				return;
			}
			
			for (std::vector<Samurai::IO::Net::NetworkInterface*>::iterator it = interfaces.begin(); it != interfaces.end(); it++)
			{
				Samurai::IO::Net::NetworkInterface* iface = *it;
				if (iface->isEnabled() && iface->isMulticast())
				{
					// FIXME: API - No pointers should be needed!
					UPnP::Worker* worker = new UPnP::Worker(iface, m_address);
					if (worker->isOK())
						m_workers.push_back(worker);
					else
						delete worker;
				}
			}
		}
		
	private:
		std::vector<UPnP::Worker*> m_workers;
		Samurai::IO::Net::InetSocketAddress m_address;
};

int main(int, char**)
{
	bool running = true;

	UPnPHandler uh;
	uh.browseServices();
	
	Samurai::IO::Net::SocketMonitor* monitor = Samurai::IO::Net::SocketMonitor::getInstance();
	while (running)
	{
		monitor->wait(1000);
	}
}
