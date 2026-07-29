/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_NOTIFIER_H
#define HAVE_SAMURAI_NOTIFIER_H

#include <samurai/samurai.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/socketmonitor.h>

#include <functional>
#include <memory>

namespace Samurai {
namespace IO {
namespace Net {

/**
 * Wakes the socket monitor from another thread.
 *
 * A thread with something to hand to the loop calls notify(); the read end is
 * monitored like any other socket, so wait() returns at once rather than
 * blocking out its timeout, and the callback runs on the loop thread.
 *
 * The byte carries nothing - it says only that the callback should run - which
 * has two consequences worth relying on. A write that finds the buffer full is
 * dropped, because a full buffer already means a wakeup is pending. And several
 * notifications coalesce into one callback, so the callback must look at
 * whatever state it shares with the waking thread rather than assume one byte
 * means one item.
 *
 * A socketpair rather than a pipe: the monitor keys its registry on socket
 * descriptors, and on Windows a pipe cannot be waited on at all.
 */
class Notifier final : public SocketBase {
	public:
		/**
		 * @param on_notified runs on the loop thread. Fixed at construction, so
		 *        no thread can observe it changing.
		 * @return null if the socketpair could not be created.
		 */
		template<typename... Args>
		static std::shared_ptr<Notifier> create(Args&&... args)
		{
			std::shared_ptr<Notifier> self(new Notifier(std::forward<Args>(args)...));
			if (self->getFD() == INVALID_SOCKET) return nullptr;
			self->initialize();
			return self;
		}

		~Notifier() override;

		/* Owns two descriptors, so a copy would close them twice. */
		Notifier(const Notifier&) = delete;
		Notifier& operator=(const Notifier&) = delete;

		/**
		 * Wake the loop. Safe to call from any thread, and from several at
		 * once.
		 */
		void notify();

	protected:
		explicit Notifier(std::function<void()> on_notified);

		void initialize() override;
		void handleMonitorEvent(SocketMonitor::Triggers trig) override;

	private:
		socket_t writer;
		std::function<void()> callback;
};

}
}
}

#endif // HAVE_SAMURAI_NOTIFIER_H
