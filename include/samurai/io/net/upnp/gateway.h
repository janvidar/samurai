/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_UPNP_GATEWAY_H
#define HAVE_SAMURAI_UPNP_GATEWAY_H

#include <samurai/io/net/http/client.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/upnp/error.h>
#include <samurai/io/net/url.h>
#include <samurai/timer.h>

#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Samurai {
namespace IO {
namespace Net {
namespace UPnP {

/** Which of a gateway's services a request is for. */
enum class ServiceKind
{
	WanIpConnection,
	WanPppConnection,
	WanIpv6FirewallControl
};

/**
 * One argument of an action.
 *
 * The order is kept on the wire: more than one gateway reads its arguments
 * positionally and refuses a request that names them all correctly in the wrong
 * order.
 */
using Argument = std::pair<std::string, std::string>;

/** What an action returned. */
struct ActionResult
{
	std::vector<Argument> arguments;

	/** The named out argument, or null when the device omitted it. */
	const std::string* find(std::string_view name) const;

	/** The named out argument, or an empty string when absent. */
	std::string value(std::string_view name) const;
};

class Action;
class Gateway;

/** Implement this to receive the outcome of one action. */
class ActionEventHandler : public Samurai::IO::Net::EventHandler
{
	public:
		ActionEventHandler() { }
		~ActionEventHandler() override { }

	protected:
		virtual void EventActionResponse(Action*, const ActionResult&) = 0;

		/**
		 * The device answered and refused.
		 *
		 * Separate from EventActionError because a refusal is neither a success
		 * nor a transport failure: the code is data the caller acts on - 725
		 * means ask again without a lease, 718 means the port is taken - and
		 * folding it into either of the others loses it.
		 *
		 * 'code' is the number as sent, so a vendor's own code survives not
		 * being one of the recognised ones.
		 */
		virtual void EventActionFault(Action*, DeviceError error, uint16_t code,
		                              std::string_view description) = 0;

		virtual void EventActionError(Action*, Error) = 0;

	friend class Action;
	friend class Gateway;
};

/**
 * One SOAP action in flight.
 *
 * The caller owns it and has to keep it alive until the handler has been
 * called. Destroying it cancels, and nothing is reported afterwards.
 */
class Action final
{
	public:
		~Action();

		Action(const Action&) = delete;
		Action& operator=(const Action&) = delete;

		const std::string& getName() const { return action; }
		ServiceKind getService() const { return kind; }

	private:
		Action(Gateway* owner, ActionEventHandler* eh, ServiceKind service,
		       std::string_view name, std::span<const Argument> arguments);

		Gateway* gateway;
		ActionEventHandler* eventHandler;
		ServiceKind kind;
		std::string action;
		std::vector<Argument> args;
		bool queued = true;

	friend class Gateway;
};

/**
 * What is needed to reach a gateway again without repeating discovery.
 *
 * Copyable, and holds no sockets, so it can be cached between calls or written
 * to a configuration file.
 */
struct GatewayInfo
{
	URL descriptionURL{""};
	URL controlURL{""};
	std::string serviceType;
	std::string usn;
	std::string friendlyName;
	int deviceVersion = 1;
	/** Set once the device has answered 725 to a lease, so later adds skip it. */
	bool permanentLeasesOnly = false;
	InetAddress localAddress;
	Samurai::Timer::clock::time_point expires{};

	bool isValid() const;
	bool hasExpired() const;
};

/** Implement this to learn that a gateway was found. */
class GatewayEventHandler : public Samurai::IO::Net::EventHandler
{
	public:
		/** One search reply, before any description has been fetched. */
		struct Candidate
		{
			URL location{""};
			std::string st;
			std::string usn;
			std::string server;
			InetAddress source;
		};

		GatewayEventHandler() { }
		~GatewayEventHandler() override { }

	protected:
		virtual void EventGatewayFound(Gateway*) = 0;
		virtual void EventGatewayError(Gateway*, Error) = 0;

		/**
		 * A search reply arrived, deduplicated. Informational: only a tool that
		 * lists what is on the network cares, so it has a default.
		 */
		virtual void EventGatewayCandidate(Gateway*, const Candidate&) { }

	friend class Gateway;
};

/**
 * An internet gateway: how to find one, and how to ask it for something.
 *
 * Actions are issued one at a time in the order they were queued. Embedded HTTP
 * servers commonly accept a single connection, and drop a concurrent second
 * request rather than refusing it, so overlapping them loses answers.
 */
class Gateway final
	: public Samurai::IO::Net::HTTP::RequestEventHandler
	, public Samurai::TimerListener
{
	public:
		enum class State { Idle, Searching, FetchingDescription, Ready, Failed };

		struct Options
		{
			/** How long a search runs before giving up. */
			std::chrono::milliseconds searchTimeout{ 3000 };
			/** Between retransmission rounds; SSDP is unreliable by design. */
			std::chrono::milliseconds searchInterval{ 500 };
			/** The MX a search advertises: replies are spread over [0, mx]. */
			uint8_t mx = 2;
			uint8_t searchRounds = 2;
			/** Multicast hop limit. A gateway is one hop away. */
			uint8_t multicastHops = 2;
			bool searchIPv4 = true;
			bool searchIPv6 = true;
			/**
			 * Require a reply's LOCATION host to be the address it came from.
			 * Without this any host on the network can name a third party.
			 */
			bool requireLocationMatchesSource = true;
			size_t maxDescriptionBytes = 256 * 1024;
			std::chrono::milliseconds httpTimeout{ 5000 };
		};

		/**
		 * Search for a gateway.
		 *
		 * The caller owns what is returned and has to keep it alive until the
		 * handler has been called, and for as long as any PortMapper built on it
		 * exists.
		 *
		 * The handler is never called from inside this: a failure is reported on
		 * the next turn of the event loop, so the caller has always stored the
		 * pointer by the time it hears about it.
		 */
		static std::unique_ptr<Gateway> discover(GatewayEventHandler* eh);
		static std::unique_ptr<Gateway> discover(GatewayEventHandler* eh,
		                                         const Options& options);

		/**
		 * Skip the search: fetch and read the description at a known location.
		 *
		 * This is the entry point for a caller that has the gateway's URL from
		 * configuration, and the one the tests drive.
		 */
		static std::unique_ptr<Gateway> describe(GatewayEventHandler* eh,
		                                         const URL& location);
		static std::unique_ptr<Gateway> describe(GatewayEventHandler* eh,
		                                         const URL& location,
		                                         const Options& options);

		/** Skip both, and be ready immediately, from what was cached. */
		static std::unique_ptr<Gateway> attach(GatewayEventHandler* eh,
		                                       const GatewayInfo& info);
		static std::unique_ptr<Gateway> attach(GatewayEventHandler* eh,
		                                       const GatewayInfo& info,
		                                       const Options& options);

		~Gateway() override;

		Gateway(const Gateway&) = delete;
		Gateway& operator=(const Gateway&) = delete;

		State getState() const { return state; }
		bool isReady() const { return state == State::Ready; }

		GatewayInfo info() const;

		const std::string& getFriendlyName() const { return friendly_name; }
		int getDeviceVersion() const { return device_version; }
		bool supportsIgd2() const { return device_version >= 2; }
		bool hasService(ServiceKind kind) const;

		/** The advertised type of a service, empty when there is none. */
		std::string getServiceType(ServiceKind kind) const;

		/**
		 * The local address that reaches the gateway, taken from the socket the
		 * description was fetched over.
		 *
		 * This is what a port mapping names as its internal client. Nothing else
		 * in the library can answer it: NetworkInterface has one address per
		 * interface and only for Version::IPv4.
		 */
		const InetAddress* getLocalAddress() const;

		/** Give up, reporting Cancelled if nothing has been reported yet. */
		void cancel();

		/**
		 * Queue an action.
		 *
		 * The caller owns what is returned; destroying it cancels the action.
		 * @return null when the gateway has no such service.
		 */
		std::unique_ptr<Action> invoke(ActionEventHandler* eh, ServiceKind kind,
		                               std::string_view action,
		                               std::span<const Argument> arguments);

		/** Remember that this gateway refuses a lease, so later adds skip one. */
		void setPermanentLeasesOnly(bool toggle) { permanent_leases_only = toggle; }
		bool getPermanentLeasesOnly() const { return permanent_leases_only; }

	private:
		Gateway(GatewayEventHandler* eh, const Options& options);

		/* HTTP::RequestEventHandler */
		void EventHttpResponse(Samurai::IO::Net::HTTP::Client*,
		                       const Samurai::IO::Net::HTTP::Response&) override;
		void EventHttpError(Samurai::IO::Net::HTTP::Client*,
		                    Samurai::IO::Net::HTTP::Status, const char*) override;

		/* TimerListener */
		void EventTimeout(Samurai::Timer* timer) override;

		void internal_startDescription(const URL& location);
		void internal_candidate(const GatewayEventHandler::Candidate& candidate);
		void internal_searchFinished();
		bool internal_tryNextCandidate();
		void internal_ready();
		void internal_fail(Error error);
		void internal_reportLater(Error error);

		void internal_pump();
		void internal_sendAction(Action* action);
		void internal_finishAction(Action* action);
		void internal_cancelAction(Action* action);

		GatewayEventHandler* eventHandler;
		Options options;
		State state = State::Idle;

		URL description_url{""};
		std::string friendly_name;
		std::string usn;
		int device_version = 1;
		bool permanent_leases_only = false;
		InetAddress local_address;
		Samurai::Timer::clock::time_point expires{};

		/* Kept opaque so the description types stay out of the public header. */
		struct Discovered;
		std::unique_ptr<Discovered> discovered;

		std::unique_ptr<Samurai::IO::Net::HTTP::Client> http;
		std::unique_ptr<Samurai::Timer> deferred;
		Error deferred_error = Error::None;

		/* Borrowed: the caller owns each Action. */
		std::vector<Action*> pending;
		Action* inflight = nullptr;
		bool reported = false;

	friend class Action;
};

}
}
}
}

#endif // HAVE_SAMURAI_UPNP_GATEWAY_H
