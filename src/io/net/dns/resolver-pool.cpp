/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/socketglue.h>
#include <samurai/io/net/dns/resolver-pool.h>
#include <samurai/io/net/notifier.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/inetaddress.h>

#include <atomic>
#include <string.h>

namespace {

using Samurai::IO::Net::DNS::ResolverPool;
using Error = Samurai::IO::Net::DNS::Resolver::Error;

Error mapResolveError(int rc)
{
	switch (rc) {
		case EAI_NONAME:  return Error::NotFound;
		case EAI_AGAIN:   return Error::TryAgain;
		case EAI_FAIL:    return Error::ServerError;
#if defined(EAI_NODATA) && EAI_NODATA != EAI_NONAME
		case EAI_NODATA:  return Error::NoAddress;
#endif
		case EAI_MEMORY:  return Error::ServerError;
		default:          return Error::Unknown;
	}
}

/*
 * NOTE: getaddrinfo(), not gethostbyname(): it is reentrant and returns both
 * address families, which matters more here than it did on the loop thread.
 */
void resolveForward(ResolverPool::Request& request)
{
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family   = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	struct addrinfo* result = nullptr;
	const int rc = ::getaddrinfo(request.query.c_str(), nullptr, &hints, &result);

	if (rc != 0)
	{
		request.error = mapResolveError(rc);
		request.ok = false;
		return;
	}

	bool found = false;

	/* getaddrinfo() returns candidates in the order the platform prefers
	   (RFC 6724 on a current system), so the first usable one is the one to
	   take. */
	for (struct addrinfo* ai = result; ai && !found; ai = ai->ai_next)
	{
		if (ai->ai_family == AF_INET && ai->ai_addrlen >= sizeof(struct sockaddr_in))
		{
			struct sockaddr_in* sin = (struct sockaddr_in*) ai->ai_addr;
			found = request.address.setRawAddress(&sin->sin_addr, sizeof(sin->sin_addr),
			                                      Samurai::IO::Net::InetAddress::Version::IPv4);
		}
		else if (ai->ai_family == AF_INET6 && ai->ai_addrlen >= sizeof(struct sockaddr_in6))
		{
			struct sockaddr_in6* sin6 = (struct sockaddr_in6*) ai->ai_addr;
			found = request.address.setRawAddress(&sin6->sin6_addr, sizeof(sin6->sin6_addr),
			                                      Samurai::IO::Net::InetAddress::Version::IPv6);
		}
	}

	::freeaddrinfo(result);

	request.ok = found;
	if (!found) request.error = Error::NoAddress;
}

}

namespace Samurai {
namespace IO {
namespace Net {
namespace DNS {

/*
 * The flag is what lets the scheduler tell a thread that has returned from a
 * slot it can reuse. jthread::joinable() stays true until the join, so it
 * cannot answer the question on its own.
 */
struct ResolverPool::Worker {
	std::atomic<bool> finished{false};
	std::thread thread;

	~Worker() { if (thread.joinable()) thread.join(); }
};

}
}
}
}

Samurai::IO::Net::DNS::ResolverPool::ResolverPool()
	: idle_timeout(std::chrono::seconds(2))
	, max_workers(4)
	, busy_workers(0)
	, sched_wakeup(false)
	, stopping(false)
	, started(false)
{
}

Samurai::IO::Net::DNS::ResolverPool::~ResolverPool()
{
	{
		std::lock_guard<std::mutex> lock(m);
		stopping = true;
		sched_wakeup = true;
	}
	cv_work.notify_all();
	cv_sched.notify_all();

	/* Workers go first: one of them may still be on its way to notify(), and
	   the notifier has to outlive that. */
	workers.clear();

	if (sched_thread && sched_thread->joinable())
		sched_thread->join();
	sched_thread.reset();

	notifier.reset();
}

/* static */
Samurai::IO::Net::DNS::ResolverPool* Samurai::IO::Net::DNS::ResolverPool::getInstance()
{
	static Samurai::IO::Net::DNS::ResolverPool pool;
	return &pool;
}

void Samurai::IO::Net::DNS::ResolverPool::startOnce()
{
	if (started) return;

	notifier = Samurai::IO::Net::Notifier::create(
		std::function<void()>([this]() { drain(); }));

	if (!notifier)
	{
		QERR("Unable to create the resolver notification socketpair");
		return;
	}

	sched_thread = std::make_unique<std::thread>([this]() { schedule(); });
	started = true;
}

void Samurai::IO::Net::DNS::ResolverPool::submit(const std::shared_ptr<Request>& request)
{
	if (!request) return;

	startOnce();
	if (!started)
	{
		/* Nothing can carry the answer back, so fail here rather than queue
		   something that will never be reported. */
		request->ok = false;
		request->error = Error::ServerError;
		if (request->handler) request->handler->EventHostError(request->error);
		return;
	}

	{
		std::lock_guard<std::mutex> lock(m);
		pending.push_back(request);
		sched_wakeup = true;
	}

	cv_work.notify_one();
	cv_sched.notify_one();
}

void Samurai::IO::Net::DNS::ResolverPool::cancel(const std::shared_ptr<Request>& request)
{
	if (!request) return;

	std::lock_guard<std::mutex> lock(m);
	request->dead = true;

	/* Still queued: drop it outright rather than let a worker spend a lookup
	   on an answer nobody will read. One already in flight cannot be stopped,
	   and the dead flag is what discards its result. */
	for (std::deque<std::shared_ptr<Request>>::iterator it = pending.begin();
	     it != pending.end(); ++it)
	{
		if (*it == request)
		{
			pending.erase(it);
			break;
		}
	}
}

void Samurai::IO::Net::DNS::ResolverPool::setMaxWorkers(size_t count)
{
	std::lock_guard<std::mutex> lock(m);
	max_workers = count ? count : 1;
}

size_t Samurai::IO::Net::DNS::ResolverPool::getMaxWorkers() const
{
	std::lock_guard<std::mutex> lock(m);
	return max_workers;
}

void Samurai::IO::Net::DNS::ResolverPool::setIdleTimeout(std::chrono::milliseconds timeout)
{
	std::lock_guard<std::mutex> lock(m);
	idle_timeout = timeout;
}

size_t Samurai::IO::Net::DNS::ResolverPool::getWorkerCount() const
{
	std::lock_guard<std::mutex> lock(m);

	size_t live = 0;
	for (const std::unique_ptr<Worker>& worker : workers)
		if (!worker->finished.load()) live++;
	return live;
}

/*
 * One evaluation per submit: waiting on 'pending is not empty' instead would
 * stay true after a spawn and run the headcount straight up to the maximum for
 * a single request.
 */
void Samurai::IO::Net::DNS::ResolverPool::schedule()
{
	for (;;)
	{
		std::unique_lock<std::mutex> lock(m);
		cv_sched.wait(lock, [this]() { return stopping || sched_wakeup; });
		sched_wakeup = false;

		if (stopping) return;

		size_t live = 0;
		for (const std::unique_ptr<Worker>& worker : workers)
			if (!worker->finished.load()) live++;

		if (pending.empty() || busy_workers < live || live >= max_workers)
			continue;

		/* A slot whose thread has returned is reused; assigning over it joins
		   the finished thread, which costs nothing at that point. */
		std::unique_ptr<Worker>* slot = nullptr;
		for (std::unique_ptr<Worker>& worker : workers)
		{
			if (worker->finished.load()) { slot = &worker; break; }
		}

		if (!slot)
		{
			workers.push_back(std::make_unique<Worker>());
			slot = &workers.back();
		}
		else
		{
			/* Assigning over the slot joins the thread that has already
			   returned, which costs nothing at that point. */
			*slot = std::make_unique<Worker>();
		}

		Worker* self = slot->get();
		self->thread = std::thread([this, self]() { work(self); });
	}
}

void Samurai::IO::Net::DNS::ResolverPool::work(Worker* self)
{
	for (;;)
	{
		std::shared_ptr<Request> request;

		{
			std::unique_lock<std::mutex> lock(m);

			/* Idle for long enough: give the thread back. */
			if (!cv_work.wait_for(lock, idle_timeout,
			                      [this]() { return stopping || !pending.empty(); }))
				break;

			if (stopping) break;

			request = std::move(pending.front());
			pending.pop_front();
			busy_workers++;
		}

		resolveForward(*request);

		{
			std::lock_guard<std::mutex> lock(m);
			completed.push_back(std::move(request));
			busy_workers--;
		}

		/* Pushed before the wakeup, so a readable byte always means the result
		   is already there to be found. */
		notifier->notify();
	}

	/* Set outside the lock and after the loop: the scheduler reuses a slot as
	   soon as it sees this, and that reuse joins the thread. */
	self->finished.store(true);
}

void Samurai::IO::Net::DNS::ResolverPool::drain()
{
	std::deque<std::shared_ptr<Request>> ready;

	{
		std::lock_guard<std::mutex> lock(m);
		ready.swap(completed);
	}

	/* Outside the lock: a handler is free to start another lookup, which comes
	   straight back to submit() for this same mutex. */
	for (const std::shared_ptr<Request>& request : ready)
	{
		if (request->dead || !request->handler) continue;

		if (request->ok)
			request->handler->EventHostFound(&request->address);
		else
			request->handler->EventHostError(request->error);
	}
}

Samurai::IO::Net::DNS::PooledResolver::PooledResolver(Samurai::IO::Net::ResolveEventHandler* eh)
	: Samurai::IO::Net::DNS::Resolver(eh)
{
}

Samurai::IO::Net::DNS::PooledResolver::~PooledResolver()
{
	if (request)
		ResolverPool::getInstance()->cancel(request);
}

void Samurai::IO::Net::DNS::PooledResolver::lookup(const char* name)
{
	if (!eventHandler) return;

	if (!name || !*name)
	{
		eventHandler->EventHostError(Error::NotFound);
		return;
	}

	request = std::make_shared<ResolverPool::Request>();
	request->query = name;
	request->handler = eventHandler;

	ResolverPool::getInstance()->submit(request);
}
