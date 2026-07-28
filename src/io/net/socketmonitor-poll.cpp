/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <algorithm>
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
	
	max = std::min<size_t>(Samurai::OS::getMaxOpenSockets(), MAXSOCK);
	list.resize(max);
	act.resize(max);
	sockets.assign(max, nullptr);
	
	for (size_t n = 0; n < max; n++)
	{
		list[n].fd = INVALID_SOCKET;
		list[n].events = 0;
		list[n].revents = 0;
		act[n].fd = INVALID_SOCKET;
		act[n].trig = Samurai::IO::Net::SocketMonitor::Triggers::None;
	}
	num = 0;
}


Samurai::IO::Net::PollSocketMonitor::~PollSocketMonitor() = default;


bool Samurai::IO::Net::PollSocketMonitor::isValid()
{
	if (getenv("SAMURAI_NO_POLL"))
	{
		return false;
	}
	return true;
}


/*
 * NOTE: the descriptor array is kept compact - entries in [0, num) are live -
 * so poll() is handed 'num' rather than 'max'; the kernel walks the argument in
 * full.
 */
void Samurai::IO::Net::PollSocketMonitor::internal_add(Samurai::IO::Net::SocketBase* socket)
{
	Samurai::IO::Net::SocketMonitor::Triggers trigger = socket->getMonitorTrigger();
	short ev = POLLHUP | POLLERR | POLLNVAL;
	if (any(trigger & Samurai::IO::Net::SocketMonitor::Triggers::Read))  ev |= POLLIN;
	if (any(trigger & Samurai::IO::Net::SocketMonitor::Triggers::Write)) ev |= POLLOUT;
	
	if (num == max)
	{
		QERR("Cannot add socket, list is full!");
		return;
	}

	const size_t n = num;

	list[n].fd = socket->getFD();
	list[n].events = ev;
	list[n].revents = 0;
	sockets[n] = socket;
	num++;

// 	QDBG("Added socket: '%d', Position=%zu, total=%zu/%zu", socket->getFD(), n, num, max);
}


void Samurai::IO::Net::PollSocketMonitor::internal_remove(Samurai::IO::Net::SocketBase* socket) {
	size_t n = 0;
	for (; n < num; n++)
		if (sockets[n] == socket)
			break;

	if (n == num) return;  // not found

	/* Move the last live entry into the hole so the array stays compact. */
	num--;
	if (n != num)
	{
		list[n] = list[num];
		sockets[n] = sockets[num];
	}

	list[num].fd = INVALID_SOCKET;
	list[num].events = 0;
	list[num].revents = 0;
	sockets[num] = 0;
}


void Samurai::IO::Net::PollSocketMonitor::internal_modify(Samurai::IO::Net::SocketBase* socket) {
	size_t n = 0;
	for (; n < num; n++)
		if (sockets[n] == socket)
			break;

	if (n == num) return; // not found
	
	Samurai::IO::Net::SocketMonitor::Triggers trigger = socket->getMonitorTrigger();
	
	short ev = POLLHUP | POLLERR | POLLNVAL;
	if (any(trigger & Samurai::IO::Net::SocketMonitor::Triggers::Read))  ev |= POLLIN;
	if (any(trigger & Samurai::IO::Net::SocketMonitor::Triggers::Write)) ev |= POLLOUT;
	list[n].events = ev;
}


void Samurai::IO::Net::PollSocketMonitor::internal_wait(int time_ms)
{
	if (num == 0) return;

	int ret = ::poll(list.data(), num, time_ms);
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
	for (size_t n = 0; n < num; n++)
	{
		Samurai::IO::Net::SocketBase* sock = sockets[n];
		if (!sock) continue;

		Samurai::IO::Net::SocketMonitor::Triggers trig = Samurai::IO::Net::SocketMonitor::Triggers::None;
		const short f = list[n].revents;
		if (f & POLLOUT) trig |= Samurai::IO::Net::SocketMonitor::Triggers::Write;
		if (f & POLLIN)  trig |= Samurai::IO::Net::SocketMonitor::Triggers::Read;

		if (f & (POLLERR | POLLNVAL)) trig |= Samurai::IO::Net::SocketMonitor::Triggers::Error;
		if (f & POLLHUP)              trig |= Samurai::IO::Net::SocketMonitor::Triggers::Close;

		if (!any(trig)) continue;

		act[act_num].fd = list[n].fd;
		act[act_num].trig = trig;
		act_num++;
	}
	
	for (size_t n = 0; n < act_num; n++)
		dispatch(act[n].fd, act[n].trig);
}


#endif // SOCKET_NOTIFY_POLL
