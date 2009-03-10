/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include "socketmonitor-backend.h"

#ifdef SOCKET_NOTIFY_POLL

#include <samurai/samurai.h>
#include <samurai/os.h>
#include <samurai/io/net/socketglue.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/socket.h>
#include <samurai/io/net/serversocket.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/datagram.h>

#include <poll.h>
#include <stdlib.h>

#define MAXSOCK 4096

Samurai::IO::Net::PollSocketMonitor::PollSocketMonitor() : Samurai::IO::Net::SocketMonitor("poll")
{
	
	max = MIN(Samurai::OS::getMaxOpenSockets(), MAXSOCK);
	list = new struct pollfd[max];
	act  = new struct poll_act[max];
	sockets = new SocketBase*[max];
	
	for (size_t n = 0; n < max; n++)
	{
		list[n].fd = INVALID_SOCKET;
		list[n].events = 0;
		list[n].revents = 0;
		act[n].sock = 0;
		act[n].trig = 0;
		sockets[n] = 0;
	}
	num = 0;
}


Samurai::IO::Net::PollSocketMonitor::~PollSocketMonitor()
{
	delete[] act;
	delete[] list;
	delete[] sockets;
}


bool Samurai::IO::Net::PollSocketMonitor::isValid()
{
	if (getenv("SAMURAI_NO_POLL"))
	{
		return false;
	}
	return true;
}


void Samurai::IO::Net::PollSocketMonitor::internal_add(Samurai::IO::Net::SocketBase* socket)
{
	int trigger = socket->getMonitorTrigger();
	short ev = POLLHUP | POLLERR | POLLNVAL;
	if (trigger & MRead)  ev |= POLLIN;
	if (trigger & MWrite) ev |= POLLOUT;
	
	size_t n = 0;
	for (; n < max; n++)
		if (list[n].fd == -1)
			break;
	
	if (n == max)
	{
		QERR("Cannot add socket, list is full!");
		return; // not found!
	}

	list[n].fd = socket->getFD();
	list[n].events = ev;
	list[n].revents = 0;
	sockets[n] = socket;
	num++;

// 	QDBG("Added socket: '%d', Position=%zu, total=%zu/%zu", socket->getFD(), n, num, max);
}


void Samurai::IO::Net::PollSocketMonitor::internal_remove(Samurai::IO::Net::SocketBase* socket) {
	size_t n = 0;
	for (; n < max; n++)
		if (list[n].fd == socket->getFD())
			break;
	
	if (n == max) return;  // not found
	
	list[n].fd = INVALID_SOCKET;
	list[n].events = 0;
	list[n].revents = 0;
	sockets[n] = 0;
	num--;
}


void Samurai::IO::Net::PollSocketMonitor::internal_modify(Samurai::IO::Net::SocketBase* socket) {
	size_t n = 0;
	for (; n < max; n++)
		if (list[n].fd == socket->getFD())
			break;

	if (n == max) return; // not found
	
	int trigger = socket->getMonitorTrigger();
	
	short ev = POLLHUP | POLLERR | POLLNVAL;
	if (trigger & MRead)  ev |= POLLIN;
	if (trigger & MWrite) ev |= POLLOUT;
	list[n].events = ev;
}


void Samurai::IO::Net::PollSocketMonitor::wait(int time_ms)
{
	int ret = ::poll(list, max, time_ms);
	if (ret == 0) return;
	
	if (ret == -1)
	{
		if (NETERROR != EINTR)
		{
			QERR("Poll error: %i, %s", NETERROR, strerror(NETERROR));
		}
		return;
	}
	
	size_t act_num = 0;
	for (size_t n = 0; n < max; n++)
	{
		Samurai::IO::Net::SocketBase* sock = sockets[n];
		if (!sock) continue;
		
		int trig = 0;
		uint16_t f = list[n].revents;
		if (f & POLLOUT) trig |= MWrite;
		if (f & POLLIN)  trig |= MRead;
		
		act[act_num].sock = sock;
		act[act_num].trig = trig;
		act_num++;
	}
	
	for (size_t n = 0; n < act_num; n++)
		handleSocketEvent(act[n].sock, act[n].trig);
}


#endif // SOCKET_NOTIFY_POLL
