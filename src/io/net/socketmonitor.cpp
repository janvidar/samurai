/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/socketglue.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/socket.h>
#include <samurai/io/net/serversocket.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/datagram.h>

#include "socketmonitor-backend.h"

#ifdef SSL_SUPPORT
#include <samurai/io/net/tlsfactory.h>
#if !defined(SSL_GNUTLS) && !defined(SSL_OPENSSL)
bool Samurai::IO::Net::TlsFactory::global_init() { return false; }
bool Samurai::IO::Net::TlsFactory::global_deinit() {return false; }
#endif
#endif // SSL_SUPPORT

/* FIXME: Must not initialize SSL and WSA for each SocketMonitor created! */

Samurai::IO::Net::SocketMonitor::SocketMonitor(const char* name_) : name(name_)
{
#ifdef WINSOCK
	QDBG("Initializing Winsock library...");
	WSAData wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != NO_ERROR) {
		QERR("ERROR: Unable to initialize winsock");
		abort();
	}
#endif

#ifdef SSL_SUPPORT
	Samurai::IO::Net::TlsFactory::global_init();
#endif // SSL_SUPPORT
}


Samurai::IO::Net::SocketMonitor::~SocketMonitor()
{
#ifdef SSL_SUPPORT
	Samurai::IO::Net::TlsFactory::global_deinit();
#endif // SSL_SUPPORT

#ifdef WINSOCK
	WSACleanup();
#endif
}


void Samurai::IO::Net::SocketMonitor::add(Samurai::IO::Net::SocketBase* socket)
{
	if (!socket || (int) socket->getFD() == (int) INVALID_SOCKET)
	{
		return;
	}
	
	internal_add(socket);
}


void Samurai::IO::Net::SocketMonitor::remove(Samurai::IO::Net::SocketBase* socket)
{
	if (!socket || (int) socket->sd == (int) INVALID_SOCKET)
	{
		return;
	}
	internal_remove(socket);
}


void Samurai::IO::Net::SocketMonitor::modify(Samurai::IO::Net::SocketBase* socket)
{
	if (!socket || (int) socket->sd == (int) INVALID_SOCKET)
	{
		QERR("Trying to modify invalid socket: %p, sd=%d", socket, socket ? (int) socket->sd : -666);
		return;
	}
	internal_modify(socket);
}


void Samurai::IO::Net::SocketMonitor::handleSocketEvent(Samurai::IO::Net::SocketBase* sock, int trig)
{
	Samurai::IO::Net::Socket* socket = dynamic_cast<Samurai::IO::Net::Socket*>((Samurai::IO::Net::SocketBase*) sock);
	Samurai::IO::Net::ServerSocket* server = dynamic_cast<Samurai::IO::Net::ServerSocket*>((Samurai::IO::Net::SocketBase*) sock);
	Samurai::IO::Net::DatagramSocket* udp = dynamic_cast<Samurai::IO::Net::DatagramSocket*>((Samurai::IO::Net::SocketBase*) sock);
	

	if (trig & MRead)
	{
		if (socket)
		{
			switch (socket->state)
			{
				case Connected:
				{
					char buf[1];
#ifndef SAMURAI_WINSOCK
					int x = recv(socket->sd, &buf, 1, MSG_PEEK | MSG_DONTWAIT);
#else
					int x = recv(socket->sd, (char*) &buf, 1, MSG_PEEK);
#endif
					if (x > 0)
					{
						socket->internal_canRead();
					}
					else if (x == 0)
					{
						socket->internal_closed();
					}
					else if (x == -1)
					{
						if (NETERROR != EAGAIN && NETERROR != EINTR)
						{
							QERR("Socket recv error: %d (%s) (x=%d)\n", NETERROR, strerror(NETERROR), x);
							socket->internal_error(NETERROR);
						}
					}
				}
				break;
				
				
				case SSLBye:
				case SSLHandshake:
				case SSLConnected:
					socket->internal_canWrite();
					break;
				
				
				default:
					// ignore
					break;
			}
		} else if (udp) {
			udp->internal_canRead();
		} else if (server) {
			server->internal_accept();
		}
	}
	
	if (trig & MWrite)
	{
		Samurai::IO::Net::Socket* socket = dynamic_cast<Samurai::IO::Net::Socket*>((Samurai::IO::Net::SocketBase*) sock);
		if (socket)
		{
			switch (socket->state)
			{
				case Connecting:
					socket->internal_connected();
					break;
						
				case SSLBye:
				case SSLHandshake:
				case Connected:
				case SSLConnected:
					socket->internal_canWrite();
					break;
						
				default:
					// ERROR!
					break;
			}
		}
		return;
	}
	
}

Samurai::IO::Net::SocketMonitor* Samurai::IO::Net::SocketMonitor::socket_monitor = 0;


bool Samurai::IO::Net::SocketMonitor::setSocketMonitor(Samurai::IO::Net::SocketMonitor* monitor)
{
	if (!monitor)
	{
		socket_monitor = 0;
		return true;
	}
	
	if (socket_monitor)
		return false;
	
	if (!monitor->isValid())
		return false;
	
	socket_monitor = monitor;
	
	QDBG("SocketMonitor: Using %s backend", monitor->name);
	return true;
}



Samurai::IO::Net::SocketMonitor* Samurai::IO::Net::SocketMonitor::getInstance()
{
	if (socket_monitor)
		return socket_monitor;
	
	Samurai::IO::Net::SocketMonitor* monitor = 0;
	
#ifdef SOCKET_NOTIFY_EPOLL
	if (!monitor)
	{
		monitor = new Samurai::IO::Net::EPollSocketMonitor();
		if (!monitor->isValid())
		{
			delete monitor;
			monitor = 0;
		}
	}
#endif // SOCKET_NOTIFY_EPOLL

#ifdef SOCKET_NOTIFY_KQUEUE
	if (!monitor)
	{
		monitor = new Samurai::IO::Net::KQueueSocketMonitor();
		if (!monitor->isValid())
		{
			delete monitor;
			monitor = 0;
		}
	}
#endif // SOCKET_NOTIFY_KQUEUE


#ifdef SOCKET_NOTIFY_POLL
	if (!monitor)
	{
		monitor = new Samurai::IO::Net::PollSocketMonitor();
		if (!monitor->isValid())
		{
			delete monitor;
			monitor = 0;
		}
	}
#endif // SOCKET_NOTIFY_POLL

#ifdef SOCKET_NOTIFY_SELECT
	if (!monitor)
	{
		monitor = new Samurai::IO::Net::SelectSocketMonitor();
		if (!monitor->isValid())
		{
			delete monitor;
			monitor = 0;
		}
	}
#endif // SOCKET_NOTIFY_SELECT
	
	setSocketMonitor(monitor);
// 	canDelete = true;
	
	if (!socket_monitor)
	{
		QERR("Unable to find a suitable socket monitor.");
	}
	
	return socket_monitor;
}


