/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/eventloop.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/upnp/gateway.h>
#include <samurai/io/net/upnp/portmap.h>
#include <samurai/timer.h>

#include <mutex>

namespace {

/*
 * The cached gateway, kept as facts rather than as a live object: a Gateway is
 * bound to the loop it was made on and owns sockets on it, so a cached one would
 * outlive that loop or be handed to a call driving another. Facts can be
 * attached to whichever loop the next call is given.
 */
Samurai::IO::Net::UPnP::GatewayInfo cached_gateway;

/** Set by setBlockingGateway(), used instead of searching. */
Samurai::IO::Net::URL configured_location("");

/* Loops on different threads may each make a blocking call. */
std::mutex cache_mutex;

/*
 * Refuses a nested call rather than letting it recurse. This catches the common
 * accident - calling a blocking helper from inside a handler of this same
 * subsystem. A loop belonging to something else cannot be detected at all.
 */
thread_local bool inside_blocking_call = false;

class Guard
{
	public:
		Guard() { inside_blocking_call = true; }
		~Guard() { inside_blocking_call = false; }

		Guard(const Guard&) = delete;
		Guard& operator=(const Guard&) = delete;
};

/** What one blocking call is waiting for. */
class Blocking
	: public Samurai::IO::Net::UPnP::GatewayEventHandler
	, public Samurai::IO::Net::UPnP::PortMapperEventHandler
{
	public:
		using Error = Samurai::IO::Net::UPnP::Error;
		using DeviceError = Samurai::IO::Net::UPnP::DeviceError;
		using Mapping = Samurai::IO::Net::UPnP::Mapping;
		using MappingKey = Samurai::IO::Net::UPnP::MappingKey;

		bool ready = false;
		bool done = false;
		Samurai::IO::Net::UPnP::MapResult result;

	protected:
		void EventGatewayFound(Samurai::IO::Net::UPnP::Gateway*) override
		{ ready = true; }

		void EventGatewayError(Samurai::IO::Net::UPnP::Gateway*, Error error) override
		{
			result.error = error;
			done = true;
		}

		void EventMappingAdded(Samurai::IO::Net::UPnP::PortMapper*,
		                       const Mapping& mapping) override
		{
			result.externalPort = mapping.externalPort;
			result.internalClient = mapping.internalClient;
			result.error = Error::None;
			done = true;
		}

		void EventMappingRemoved(Samurai::IO::Net::UPnP::PortMapper*,
		                         const MappingKey& key) override
		{
			result.externalPort = key.externalPort;
			result.error = Error::None;
			done = true;
		}

		void EventExternalAddress(Samurai::IO::Net::UPnP::PortMapper*,
		                          const Samurai::IO::Net::InetAddress& address) override
		{
			result.externalAddress = address;
			result.error = Error::None;
			done = true;
		}

		void EventMapperError(Samurai::IO::Net::UPnP::PortMapper*, Error error,
		                      DeviceError device, uint16_t code) override
		{
			result.error = error;
			result.deviceError = device;
			result.code = code;
			done = true;
		}
};

/**
 * Run the loop's sockets and timers until 'done' or the deadline.
 *
 * The slice is capped so the outer deadline is honoured even when no timer is
 * armed.
 */
template<typename Predicate>
void pump_until(Samurai::EventLoop& loop, Predicate done,
                Samurai::Timer::clock::time_point deadline)
{
	while (!done() && Samurai::Timer::clock::now() < deadline)
	{
		const int next = loop.timers().timeToNext();
		const int slice = (next < 0 || next > 50) ? 50 : next;
		loop.monitor().wait(slice);
	}
}

/**
 * Bring a gateway to Ready, from the cache when it is still good.
 *
 * @return null when none could be reached, with 'state' saying why.
 */
std::unique_ptr<Samurai::IO::Net::UPnP::Gateway>
reach_gateway(Samurai::EventLoop& loop, Blocking& state,
              Samurai::Timer::clock::time_point deadline)
{
	using namespace Samurai::IO::Net::UPnP;

	GatewayInfo cached;
	Samurai::IO::Net::URL location("");
	{
		const std::lock_guard<std::mutex> lock(cache_mutex);
		cached = cached_gateway;
		location = configured_location;
	}

	std::unique_ptr<Gateway> gateway;

	if (cached.isValid() && !cached.hasExpired())
		gateway = Gateway::attach(loop, &state, cached);
	else if (location.isValid())
		gateway = Gateway::describe(loop, &state, location);
	else
		gateway = Gateway::discover(loop, &state);

	if (!gateway)
	{
		state.result.error = Error::NoGateway;
		return nullptr;
	}

	pump_until(loop, [&state] { return state.ready || state.done; }, deadline);

	if (!state.ready)
	{
		/* A stale cache is the likely cause of a failure that arrived at once,
		   so it is dropped rather than left to fail the next call too. */
		const std::lock_guard<std::mutex> lock(cache_mutex);
		cached_gateway = GatewayInfo();
		if (state.result.error == Error::None) state.result.error = Error::NoGateway;
		return nullptr;
	}

	const std::lock_guard<std::mutex> lock(cache_mutex);
	cached_gateway = gateway->info();
	return gateway;
}

}


void Samurai::IO::Net::UPnP::setBlockingGateway(const URL& location)
{
	const std::lock_guard<std::mutex> lock(cache_mutex);
	configured_location = location;
	cached_gateway = GatewayInfo();
}


void Samurai::IO::Net::UPnP::resetBlockingCache()
{
	const std::lock_guard<std::mutex> lock(cache_mutex);
	cached_gateway = GatewayInfo();
}


Samurai::IO::Net::UPnP::MapResult
Samurai::IO::Net::UPnP::mapPortBlocking(Samurai::EventLoop& loop, Protocol protocol,
                                        uint16_t internalPort, uint16_t externalPort,
                                        std::string_view description,
                                        std::chrono::seconds lease,
                                        std::chrono::milliseconds timeout)
{
	MapResult refused;
	if (inside_blocking_call) { refused.error = Error::Reentrant; return refused; }

	const Guard guard;
	const Samurai::Timer::clock::time_point deadline =
		Samurai::Timer::clock::now() + timeout;

	Blocking state;
	std::unique_ptr<Gateway> gateway = reach_gateway(loop, state, deadline);
	if (!gateway) return state.result;

	std::unique_ptr<PortMapper> mapper = PortMapper::create(loop, &state, gateway.get());
	if (!mapper) { state.result.error = Error::NoService; return state.result; }

	Mapping mapping;
	mapping.protocol = protocol;
	mapping.internalPort = internalPort;
	mapping.externalPort = externalPort ? externalPort : internalPort;
	mapping.description = std::string(description);
	mapping.lease = lease;

	/* A caller that did not care which port it got asked for none, so the
	   gateway is allowed to choose. */
	if (externalPort) mapper->addMapping(mapping);
	else mapper->addAnyMapping(mapping);

	pump_until(loop, [&state] { return state.done; }, deadline);

	if (!state.done) state.result.error = Error::Network;
	return state.result;
}


Samurai::IO::Net::UPnP::MapResult
Samurai::IO::Net::UPnP::unmapPortBlocking(Samurai::EventLoop& loop, Protocol protocol, uint16_t externalPort,
                                          std::chrono::milliseconds timeout)
{
	MapResult refused;
	if (inside_blocking_call) { refused.error = Error::Reentrant; return refused; }

	const Guard guard;
	const Samurai::Timer::clock::time_point deadline =
		Samurai::Timer::clock::now() + timeout;

	Blocking state;
	std::unique_ptr<Gateway> gateway = reach_gateway(loop, state, deadline);
	if (!gateway) return state.result;

	std::unique_ptr<PortMapper> mapper = PortMapper::create(loop, &state, gateway.get());
	if (!mapper) { state.result.error = Error::NoService; return state.result; }

	MappingKey key;
	key.protocol = protocol;
	key.externalPort = externalPort;

	mapper->deleteMapping(key);
	pump_until(loop, [&state] { return state.done; }, deadline);

	if (!state.done) state.result.error = Error::Network;
	return state.result;
}


Samurai::IO::Net::UPnP::MapResult
Samurai::IO::Net::UPnP::getExternalAddressBlocking(Samurai::EventLoop& loop,
                                                   std::chrono::milliseconds timeout)
{
	MapResult refused;
	if (inside_blocking_call) { refused.error = Error::Reentrant; return refused; }

	const Guard guard;
	const Samurai::Timer::clock::time_point deadline =
		Samurai::Timer::clock::now() + timeout;

	Blocking state;
	std::unique_ptr<Gateway> gateway = reach_gateway(loop, state, deadline);
	if (!gateway) return state.result;

	std::unique_ptr<PortMapper> mapper = PortMapper::create(loop, &state, gateway.get());
	if (!mapper) { state.result.error = Error::NoService; return state.result; }

	mapper->getExternalAddress();
	pump_until(loop, [&state] { return state.done; }, deadline);

	if (!state.done) state.result.error = Error::Network;
	return state.result;
}
