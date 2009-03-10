/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include "socketmonitor-backend.h"

#ifdef SOCKET_NOTIFY_SELECT

#include <samurai/os.h>
#include <samurai/io/net/socketglue.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/socket.h>
#include <samurai/io/net/serversocket.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/datagram.h>

#define MAXSOCK 1024

Samurai::IO::Net::SelectSocketMonitor::SelectSocketMonitor() : Samurai::IO::Net::SocketMonitor("select")
{
	max = MIN(Samurai::OS::getMaxOpenSockets(), MAXSOCK);
	act = new struct poll_act[max];
	for (size_t n = 0; n < max; n++)
	{
		act[n].sock = 0;
		act[n].trig = 0;
	}
}


Samurai::IO::Net::SelectSocketMonitor::~SelectSocketMonitor()
{
	delete[] act;
}

bool Samurai::IO::Net::SelectSocketMonitor::isValid()
{
	return true;
}


void Samurai::IO::Net::SelectSocketMonitor::internal_add(Samurai::IO::Net::SocketBase* socket)
{
	sockets.push_back(socket);
}


void Samurai::IO::Net::SelectSocketMonitor::internal_remove(Samurai::IO::Net::SocketBase* socket) {
	std::vector<SocketBase*>::iterator it = sockets.begin();
	for (; it != sockets.end(); it++)
	{
		SocketBase* sock = (*it);
		if (sock == socket)
		{
			sockets.erase(it);
			return;
		}
	}
}


void Samurai::IO::Net::SelectSocketMonitor::internal_modify(Samurai::IO::Net::SocketBase*)
{
}


void Samurai::IO::Net::SelectSocketMonitor::wait(int time_ms) {
	fd_set rfds;
	fd_set wfds;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);

	socket_t maxfd = INVALID_SOCKET;
	std::vector<SocketBase*>::iterator it = sockets.begin();
	for (; it != sockets.end(); it++) {
		SocketBase* sock = (*it);
		if (sock->getMonitorTrigger() & MWrite)
		{
			FD_SET(sock->getFD(), &wfds);
			if (maxfd < sock->getFD())
				maxfd = sock->getFD();
		}
		else if (sock->getMonitorTrigger() & MRead)
		{
			FD_SET(sock->getFD(), &rfds);
			if (maxfd < sock->getFD())
				maxfd = sock->getFD();
		}
	}

	struct ::timeval timeout;
	timeout.tv_sec = (time_ms / 1000);
	timeout.tv_usec = ((time_ms % 1000) * 1000);

	int ret = ::select(maxfd + 1, &rfds, &wfds, 0, &timeout);
	if (ret == 0) return;

	if (ret == -1)
	{
		if (NETERROR != EINTR)
		{
			QERR("Select error: %i, %s", NETERROR, strerror(NETERROR));
		}
		return;
	}

	size_t act_num = 0;
	it = sockets.begin();
	for (; it != sockets.end(); it++)
	{
		SocketBase* sock = (*it);
		int trig = 0;
		if (FD_ISSET(sock->getFD(), &wfds)) trig |= MWrite;
		if (FD_ISSET(sock->getFD(), &rfds)) trig |= MRead;
		
		act[act_num].sock = sock;
		act[act_num].trig = trig;
		act_num++;
	}
	
	for (size_t n = 0; n < act_num; n++)
		handleSocketEvent(act[n].sock, act[n].trig);
}

#endif // SOCKET_NOTIFY_SELECT
