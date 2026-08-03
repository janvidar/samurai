/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_UPNP_PORTMAPPER_H
#define HAVE_SAMURAI_UPNP_PORTMAPPER_H

#include <samurai/io/net/inetaddress.h>
#include <samurai/io/net/upnp/error.h>
#include <samurai/io/net/upnp/gateway.h>
#include <samurai/timer.h>

#include <chrono>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Samurai {
namespace IO {
namespace Net {
namespace UPnP {

enum class Protocol { TCP, UDP };

/** The spelling the protocol takes on the wire. */
const char* toString(Protocol protocol);

/**
 * What identifies a mapping, and so what DeletePortMapping takes.
 *
 * A mapping is keyed by what arrives at the gateway, not by what it forwards
 * to: the internal client is not part of it.
 */
struct MappingKey
{
	Protocol protocol = Protocol::TCP;
	uint16_t externalPort = 0;
	/** Empty is the wildcard, which is what almost every caller wants. */
	std::string remoteHost;

	bool operator==(const MappingKey&) const = default;
};

/** A port mapping, as asked for and as granted. */
struct Mapping
{
	Protocol protocol = Protocol::TCP;
	uint16_t externalPort = 0;
	uint16_t internalPort = 0;
	/**
	 * Left unset to mean "the address that faces the gateway", which the
	 * PortMapper fills in from Gateway::getLocalAddress().
	 */
	InetAddress internalClient;
	std::string remoteHost;
	std::string description;
	/** Zero is a permanent mapping. */
	std::chrono::seconds lease{ 0 };
	bool enabled = true;

	MappingKey key() const;
};

class PortMapper;

/** Implement this to follow what a PortMapper is doing. */
class PortMapperEventHandler : public Samurai::IO::Net::EventHandler
{
	public:
		PortMapperEventHandler() { }
		~PortMapperEventHandler() override { }

	protected:
		/**
		 * The mapping as granted.
		 *
		 * When addAnyMapping() moved the port, or a conflict made the mapper
		 * walk to the next one, externalPort is the port that was actually
		 * granted - and so the one to delete later.
		 */
		virtual void EventMappingAdded(PortMapper*, const Mapping&) = 0;
		virtual void EventMappingRemoved(PortMapper*, const MappingKey&) = 0;

		virtual void EventMappingRenewed(PortMapper*, const Mapping&) { }
		virtual void EventMappingFound(PortMapper*, const Mapping&) { }
		virtual void EventMappingList(PortMapper*, std::span<const Mapping>) { }
		virtual void EventExternalAddress(PortMapper*, const InetAddress&) { }

		/**
		 * Something failed. 'device' is meaningful when 'error' is
		 * Error::Device, and 'code' is the number the gateway sent so a
		 * vendor's own survives not being recognised.
		 */
		virtual void EventMapperError(PortMapper*, Error error,
		                              DeviceError device, uint16_t code) = 0;

	friend class PortMapper;
};

/**
 * Adds, removes and renews port mappings on a gateway.
 *
 * The gateway is borrowed and has to outlive this.
 */
class PortMapper final
	: public ActionEventHandler
	, public Samurai::TimerListener
{
	public:
		struct Options
		{
			/**
			 * Long enough to survive, short enough that a mapping left behind
			 * by a crash goes away by itself. Which is why asking for a finite
			 * lease matters even though this renews it.
			 */
			std::chrono::seconds lease{ 7200 };
			/**
			 * Many gateways refuse any lease with error 725. Ask again for a
			 * permanent one rather than reporting a failure the caller can do
			 * nothing about.
			 */
			bool fallBackToPermanent = true;
			bool renewAutomatically = true;
			/** Renew at this fraction of the granted lease. */
			double renewAt = 0.5;
			/** A permanent mapping is re-added this often: gateways reboot. */
			std::chrono::minutes permanentRefresh{ 30 };
			/** How far addAnyMapping() walks when it has to try ports itself. */
			uint16_t conflictRetries = 8;
		};

		static std::unique_ptr<PortMapper> create(PortMapperEventHandler* eh,
		                                          Gateway* gateway);
		static std::unique_ptr<PortMapper> create(PortMapperEventHandler* eh,
		                                          Gateway* gateway,
		                                          const Options& options);

		~PortMapper() override;

		PortMapper(const PortMapper&) = delete;
		PortMapper& operator=(const PortMapper&) = delete;

		void addMapping(const Mapping& mapping);

		/**
		 * Ask the gateway to pick an external port.
		 *
		 * AddAnyPortMapping on a version 2 device. On a version 1 device, or one
		 * that does not implement it, consecutive AddPortMapping attempts
		 * starting from the requested port.
		 */
		void addAnyMapping(const Mapping& mapping);

		void deleteMapping(const MappingKey& key);
		void getMapping(const MappingKey& key);

		/** Walk the gateway's table until it says there is no more. */
		void listMappings(size_t maxEntries = 128);

		void getExternalAddress();

		/**
		 * Delete every mapping this object added.
		 *
		 * The destructor deliberately does not: it would have to drive the event
		 * loop during teardown, re-entering arbitrary application code at
		 * process exit. A finite lease is what cleans up instead.
		 */
		void shutdown();

		void cancel();

		/** The mappings this object believes it holds. */
		std::span<const Mapping> getMappings() const { return held; }

		const Options& getOptions() const { return options; }

	private:
		PortMapper(PortMapperEventHandler* eh, Gateway* gateway, const Options& options);

		/** What one public call turned into, and how far it has got. */
		struct Request;

		void EventActionResponse(Action*, const ActionResult&) override;
		void EventActionFault(Action*, DeviceError, uint16_t, std::string_view) override;
		void EventActionError(Action*, Error) override;

		void EventTimeout(Samurai::Timer* timer) override;

		void internal_start(std::unique_ptr<Request> request);
		void internal_issue(Request* request);
		void internal_finish(Request* request);
		void internal_report(Error error, DeviceError device, uint16_t code);

		std::vector<Argument> internal_addArguments(const Mapping& mapping,
		                                            bool anyPort) const;
		InetAddress internal_client(const Mapping& mapping) const;

		void internal_scheduleRenewal(const Mapping& mapping);
		void internal_forget(const MappingKey& key);
		Mapping* internal_find(const MappingKey& key);

		PortMapperEventHandler* eventHandler;
		Gateway* gateway;
		Options options;

		std::vector<std::unique_ptr<Request>> requests;
		std::vector<Mapping> held;

		/** One renewal timer for the whole set; the due entry is found by scan. */
		std::unique_ptr<Samurai::Timer> renewal;
		bool shutting_down = false;
};

}
}
}
}

#endif // HAVE_SAMURAI_UPNP_PORTMAPPER_H
