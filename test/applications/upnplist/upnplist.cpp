/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
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

#define UPNP_MCAST_ADDR "239.255.255.250"
#define UPNP_MCAST_PORT 1900

#define UPNP_SERVER_PORT 2869
#define UPNP_UUID "6306ce79-97c3-b487-1164-00eb1eb2c9d8"
#define UPNP_SERVER_LOCATION "http://10.20.19.132:2869/upnphost/udhisapi.dll?content=uuid:" UPNP_UUID

bool running = true;

class UPnPHandler : public Samurai::IO::Net::DatagramEventHandler, public Samurai::IO::Net::ServerSocketEventHandler {
	public:
		UPnPHandler()
		{
			port = UPNP_MCAST_PORT;
			mcastaddr = new Samurai::IO::Net::InetAddress((const char*) UPNP_MCAST_ADDR, Samurai::IO::Net::InetAddress::IPv4);
			
			/*
			// Start up UPnP root device service
			Samurai::IO::Net::InetSocketAddress srv_addr(UPNP_SERVER_PORT);
			server = new Samurai::IO::Net::ServerSocket(this, &srv_addr);
			server->listen();
			*/
			server = 0;
			
			detectNetworkingAdapters();
			
		}
		
		virtual ~UPnPHandler()
		{
			while (sockets.size())
			{
				Samurai::IO::Net::MulticastSocket* socket = sockets.back();
				sockets.pop_back();
				delete socket;
			}
			
			delete mcastaddr;
			delete server;
		}
		
		void detectNetworkingAdapters()
		{
			std::vector<Samurai::IO::Net::NetworkInterface*> interfaces;
			std::vector<Samurai::IO::Net::NetworkInterface*>::iterator it;
			
			bool ok = Samurai::IO::Net::NetworkInterface::getInterfaces(interfaces);
			if (!ok) {
				QERR("Unable to detect networking devices");
				return;
			}
			
			Samurai::IO::Net::MulticastSocket* socket = new Samurai::IO::Net::MulticastSocket(this, port);
			socket->listen();
			socket->setLoopbackMode(true);
			
			
			// socket->setTimeToLive(5);
			

			
			for (it = interfaces.begin(); it != interfaces.end(); it++) {
				Samurai::IO::Net::NetworkInterface* iface = *it;
				if (iface->isEnabled() && iface->isMulticast())
				{
					QDBG("Multicast IF: #%d '%s': Addr=%s/%s",
					 	iface->getHandle(),
						iface->getName(),
						iface->getAddress() ? iface->getAddress()->toString() : "n/a",
						iface->getNetmask() ? iface->getNetmask()->toString() : "n/a",
						iface->getBroadcastAddress() ? iface->getBroadcastAddress()->toString() : "n/a",
						iface->getDestinationAddress() ? iface->getDestinationAddress()->toString() : "n/a",
						iface->getHWAddress() ? iface->getHWAddress()->getAddress() : "n/a"
					);
			
					socket->setInterface(iface);
					
					if (!socket->join(mcastaddr, UPNP_MCAST_PORT))
					{
						delete socket;
					}
					else
					{
						sockets.push_back(socket);
					}
				}
			}
			
		}
		
		void notifyLocalServiceStartup()
		{
			const char* query = "NOTIFY * HTTP/1.1\r\n"
								"Host:" UPNP_MCAST_ADDR ":1900\r\n"
								"Cache-Control:max-age=600\r\n"
								"NT:uuid:" UPNP_UUID "\r\n"
								"NTS:ssdp:alive\r\n"
								"USN:uuid:" UPNP_UUID "\r\n"
								"Server: Samurai/2.0 UPnP/1.0\r\n"
								"\r\n";
			
			
			Samurai::IO::Net::DatagramPacket packet(query);
			Samurai::IO::Net::InetSocketAddress iaddr(*mcastaddr, UPNP_MCAST_PORT);
			packet.setAddress(&iaddr);
			send(packet);

		}
		
		void notifyLocalServiceShutdown()
		{
			const char* query = "NOTIFY * HTTP/1.1\r\n"
								"Host:" UPNP_MCAST_ADDR ":1900\r\n"
								"NT:uuid:" UPNP_UUID "\r\n"
								"NTS:ssdp:byebye\r\n"
								"\r\n";
			
			
			Samurai::IO::Net::DatagramPacket packet(query);
			Samurai::IO::Net::InetSocketAddress iaddr(*mcastaddr, UPNP_MCAST_PORT);
			packet.setAddress(&iaddr);
			send(packet);
		}
				
		
		void browseServices()
		{
			const char* query = "M-SEARCH * HTTP/1.1\r\n"
								"Host:239.255.255.250:1900\r\n"
								"ST:ssdp:rootdevice\r\n"
								"Man:\"ssdp:discover\"\r\n"
								"MX:3\r\n"
								"\r\n\0";
			/*
			const char* query = "M-SEARCH * HTTP/1.1\r\n"
							 	"HOST: 239.255.255.250:1900\r\n"
								"ST:urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
								"MAN:\"ssdp:discover\"\r\n"
								"MX:3\r\n"
								"\r\n\0";
			*/
			
			Samurai::IO::Net::DatagramPacket packet(query);
			Samurai::IO::Net::InetSocketAddress iaddr(*mcastaddr, UPNP_MCAST_PORT);
			packet.setAddress(&iaddr);
			
			for (int i = 0; i < 3; i++)
				send(packet);
		}
		
		
		void send(Samurai::IO::Net::DatagramPacket& packet)
		{
			if (!sockets.size()) return;
			std::vector<Samurai::IO::Net::MulticastSocket*>::iterator it;
			for (it = sockets.begin(); it != sockets.end(); it++)
			{
				(*it)->send(&packet);
			}
		}
		
	protected:
		void EventGotDatagram(Samurai::IO::Net::DatagramSocket*, Samurai::IO::Net::DatagramPacket* packet)
		{
			Samurai::IO::Buffer* buffer = packet->getBuffer();
			printf("Got a packet (%d bytes) from %s:\n", (int) buffer->size(), packet->getAddress() ? packet->getAddress()->toString() : "'wtf?'");
			char* buf = buffer->memdup(0, buffer->size());
			printf("[%s]\n", buf);
			free(buf);
			
			// debugBuf(buffer);
		}
		
		void EventDatagramError(const Samurai::IO::Net::DatagramSocket*, const char*)
		{
			puts("Got an error");
			running = false;
		}
		
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
		std::vector<Samurai::IO::Net::MulticastSocket*> sockets;
		uint16_t port;
		Samurai::IO::Net::InetAddress* mcastaddr;
		Samurai::IO::Net::ServerSocket* server;
		
};

extern void get_if();

int main(int, char**) {
	running = true;

	UPnPHandler uh;
	
	// uh.notifyLocalServiceStartup();
	uh.browseServices();
	
	
	Samurai::IO::Net::SocketMonitor* monitor = Samurai::IO::Net::SocketMonitor::getInstance();
	while (running) {
		monitor->wait(10);
	}
	
	// uh.notifyLocalServiceShutdown();
	
}
