/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_DNSRESOLVER_POOL_H
#define HAVE_SAMURAI_DNSRESOLVER_POOL_H

#include <samurai/samurai.h>
#include <samurai/io/net/dns/resolver.h>
#include <samurai/io/net/inetaddress.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Samurai {
namespace IO {
namespace Net {

class Notifier;

namespace DNS {

/**
 * Runs name lookups on worker threads and reports them on the loop thread.
 *
 * getaddrinfo() blocks for as long as the network takes, which is why it cannot
 * be called from the socket loop. Workers call it instead and push the result
 * onto a queue; a Notifier wakes the loop, which drains the queue and calls the
 * handlers.
 *
 * Threading, in full:
 *
 * - submit(), cancel() and the drain run on the loop thread, and so do the
 *   construction and destruction of every Resolver. That is what makes the
 *   cancellation check race-free rather than merely lucky.
 * - Two queues under one mutex. The mutex is never held across a handler call:
 *   a handler may start another lookup and come straight back here for it.
 * - The notification carries no payload, so several completions coalesce into
 *   one wakeup and the drain always empties the whole queue.
 *
 * A cancelled request is marked dead rather than interrupted: getaddrinfo()
 * cannot be cancelled, so a lookup nobody wants any more simply completes into
 * a result that is discarded. It still occupies a worker until the platform
 * gives up on it, so four unreachable name servers keep four workers busy for
 * as long as resolv.conf tells them to keep trying.
 */
class ResolverPool {
	public:
		enum class Kind { Forward, Reverse };

		struct Request {
			Kind kind = Kind::Forward;
			std::string query;

			/* Written by a worker before the request reaches the completed
			   queue, read on the loop thread after it has. The queue transfer
			   under the mutex is what orders the two. */
			bool ok = false;
			Samurai::IO::Net::InetAddress address;
			std::string name;
			Resolver::Error error = Resolver::Error::Unknown;

			/* Loop thread only. */
			Samurai::IO::Net::ResolveEventHandler* handler = nullptr;
			bool dead = false;
		};

		static ResolverPool* getInstance();

		ResolverPool();
		~ResolverPool();

		ResolverPool(const ResolverPool&) = delete;
		ResolverPool& operator=(const ResolverPool&) = delete;

		/** Queue a lookup. Loop thread only. */
		void submit(const std::shared_ptr<Request>& request);

		/** Stop caring about a lookup. Loop thread only. */
		void cancel(const std::shared_ptr<Request>& request);

		/** Most workers to run at once. Four by default. */
		void setMaxWorkers(size_t count);
		size_t getMaxWorkers() const;

		/**
		 * How long a worker waits for more work before giving up its thread.
		 * Long enough by default that a run of lookups does not pay for a
		 * thread each; the tests shorten it rather than wait it out.
		 */
		void setIdleTimeout(std::chrono::milliseconds timeout);
		std::chrono::milliseconds getIdleTimeout() const;

		/** Live worker threads, for the tests. */
		size_t getWorkerCount() const;

	private:
		struct Worker;

		void schedule();
		void work(Worker* self);
		void drain();
		void startOnce();

		mutable std::mutex m;
		std::condition_variable cv_work;
		std::condition_variable cv_sched;

		std::deque<std::shared_ptr<Request>> pending;
		std::deque<std::shared_ptr<Request>> completed;

		std::vector<std::unique_ptr<Worker>> workers;
		std::unique_ptr<std::thread> sched_thread;
		std::shared_ptr<Samurai::IO::Net::Notifier> notifier;

		std::chrono::milliseconds idle_timeout;
		size_t max_workers;
		size_t busy_workers;
		bool sched_wakeup;
		bool stopping;
		bool started;
};

/**
 * The Resolver the factory hands out. It owns nothing but a handle on the
 * request it queued, which it marks dead on the way out.
 */
class PooledResolver final : public Resolver {
	public:
		explicit PooledResolver(Samurai::IO::Net::ResolveEventHandler* eventHandler);
		~PooledResolver() override;

		void lookup(const char* name) override;

		/** Reverse lookup: report the name this address answers to. */
		void lookupAddress(const Samurai::IO::Net::InetAddress& address);

	private:
		std::shared_ptr<ResolverPool::Request> request;
};

}
}
}
}

#endif // HAVE_SAMURAI_DNSRESOLVER_POOL_H
