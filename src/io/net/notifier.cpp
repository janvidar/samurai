/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/socketglue.h>
#include <samurai/io/net/notifier.h>

#include <string.h>

#ifdef SAMURAI_POSIX
#include <fcntl.h>
#endif

namespace {

/* setNonBlocking() works on the inherited descriptor; the write end needs the
   same treatment and is not one. */
bool set_nonblocking(socket_t sd)
{
#ifdef SAMURAI_POSIX
	const int flags = fcntl(sd, F_GETFL, 0);
	if (flags == -1) return false;
	return fcntl(sd, F_SETFL, flags | O_NONBLOCK) != -1;
#else
	(void) sd;
	return false;
#endif
}

}

Samurai::IO::Net::Notifier::Notifier(std::function<void()> on_notified)
	: SocketBase(SocketType::Stream)
	, writer(INVALID_SOCKET)
	, callback(std::move(on_notified))
{
#ifdef SAMURAI_POSIX
	socket_t pair[2];
	if (::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0)
	{
		QERR("Unable to create a notification socketpair: %s",
		     strerror(Samurai::IO::Net::net_error()));
		return;
	}

	sd     = pair[0];
	writer = pair[1];

	/* One way by construction: the loop only reads, the waking thread only
	   writes. Shutting the unused direction on each end makes that a property
	   of the descriptors rather than a convention. */
	::shutdown(sd, SHUT_WR);
	::shutdown(writer, SHUT_RD);

	/* The drain reads until it runs out, and notify() must never park a caller
	   on a full buffer, so neither end may block. */
	if (!setNonBlocking(true) || !set_nonblocking(writer))
	{
		QERR("Unable to make the notification socketpair non-blocking: %s",
		     strerror(Samurai::IO::Net::net_error()));
		Samurai::IO::Net::socket_close(writer);
		Samurai::IO::Net::socket_close(sd);
		writer = INVALID_SOCKET;
		sd     = INVALID_SOCKET;
	}
#endif
}

Samurai::IO::Net::Notifier::~Notifier()
{
	disableMonitor();

	if (writer != INVALID_SOCKET)
	{
		Samurai::IO::Net::socket_close(writer);
		writer = INVALID_SOCKET;
	}

	if (sd != INVALID_SOCKET)
	{
		Samurai::IO::Net::socket_close(sd);
		sd = INVALID_SOCKET;
	}
}

void Samurai::IO::Net::Notifier::initialize()
{
	setMonitor(Samurai::IO::Net::SocketMonitor::Triggers::Read);
}

void Samurai::IO::Net::Notifier::notify()
{
	if (writer == INVALID_SOCKET) return;

	const char byte = 1;

	/* EAGAIN is success as far as this is concerned: bytes already waiting mean
	   the loop has a wakeup coming, and one more would say nothing new. */
	while (::send(writer, &byte, 1, Samurai::IO::Net::send_flags) == -1
	       && Samurai::IO::Net::net_error() == EINTR)
	{
		/* interrupted before anything was written; try once more */
	}
}

void Samurai::IO::Net::Notifier::handleMonitorEvent(Samurai::IO::Net::SocketMonitor::Triggers trig)
{
	if (!any(trig & Samurai::IO::Net::SocketMonitor::Triggers::Read)) return;
	if (sd == INVALID_SOCKET) return;

	/* Drained in full before the callback runs: any number of notifications can
	   arrive between two passes, and they are one wakeup, not one each. */
	char buf[64];
	while (::recv(sd, buf, sizeof(buf), 0) > 0)
	{
		/* discard: the bytes say only that something happened */
	}

	if (callback) callback();
}
