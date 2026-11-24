/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/eventloop.h>
#include <samurai/messagehandler.h>
#include <samurai/timer.h>
#include <samurai/io/net/bandwidth.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/dns/resolver-pool.h>

Samurai::EventLoop::EventLoop()
	: EventLoop(IO::Net::SocketMonitor::createDefault())
{
}

Samurai::EventLoop::EventLoop(std::unique_ptr<IO::Net::SocketMonitor> monitor)
	: poll(std::move(monitor))
	, timerManager(std::make_unique<TimerManager>())
	, counters(std::make_unique<IO::Net::BandwidthManager>())
	, messageHandler(std::make_unique<MessageHandler>())
{
	if (poll) poll->setTimers(timerManager.get());
}

Samurai::EventLoop::~EventLoop()
{
	pool.reset();

	/* Whatever outlives the loop is told so now, while the monitor it leaves is still here. */
	for (IO::Net::SocketBase* socket : sockets)
	{
		if (poll && socket->isMonitored()) poll->remove(socket);
		socket->detachFromLoop();
	}
	sockets.clear();

	messageHandler.reset();
	counters.reset();
	if (poll) poll->setTimers(nullptr);
	timerManager.reset();
	poll.reset();
}

bool Samurai::EventLoop::isValid() const
{
	return poll && poll->isValid();
}

Samurai::IO::Net::SocketMonitor& Samurai::EventLoop::monitor() { return *poll; }
Samurai::TimerManager& Samurai::EventLoop::timers() { return *timerManager; }
Samurai::MessageHandler& Samurai::EventLoop::messages() { return *messageHandler; }
Samurai::IO::Net::BandwidthManager& Samurai::EventLoop::bandwidth() { return *counters; }

Samurai::IO::Net::DNS::ResolverPool& Samurai::EventLoop::resolver()
{
	if (!pool) pool = std::make_unique<IO::Net::DNS::ResolverPool>(*this);
	return *pool;
}

void Samurai::EventLoop::wait(int time_ms)
{
	if (poll) poll->wait(time_ms);
	messageHandler->process();
}
