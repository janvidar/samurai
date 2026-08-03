/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/upnp/portmapper.h>
#include <samurai/stdc.h>
#include <samurai/util/string.h>

#include <algorithm>

/**
 * One public call, and how far it has got.
 *
 * A single call can turn into several requests: an add that is refused for
 * carrying a lease is asked again without one, and an addAnyMapping() that has
 * to walk the ports issues one per attempt. The Kind is what the response
 * handler branches on.
 */
struct Samurai::IO::Net::UPnP::PortMapper::Request
{
	enum class Kind
	{
		Add,
		AddAny,
		Delete,
		Get,
		List,
		ExternalAddress,
		Renew
	};

	Kind kind = Kind::Add;
	Mapping mapping;
	MappingKey key;

	/** Set once a lease was refused and a permanent one asked for instead. */
	bool triedPermanent = false;
	/** Set once AddAnyPortMapping turned out not to be implemented. */
	bool triedAnyPort = false;
	uint16_t attempts = 0;

	/* List only. */
	size_t index = 0;
	size_t maxEntries = 0;
	std::vector<Mapping> collected;

	std::unique_ptr<Action> action;
};


const char* Samurai::IO::Net::UPnP::toString(Samurai::IO::Net::UPnP::Protocol protocol)
{
	return (protocol == Protocol::UDP) ? "UDP" : "TCP";
}


Samurai::IO::Net::UPnP::MappingKey Samurai::IO::Net::UPnP::Mapping::key() const
{
	MappingKey out;
	out.protocol = protocol;
	out.externalPort = externalPort;
	out.remoteHost = remoteHost;
	return out;
}


std::unique_ptr<Samurai::IO::Net::UPnP::PortMapper>
Samurai::IO::Net::UPnP::PortMapper::create(PortMapperEventHandler* eh, Gateway* gateway)
{
	const Options options;
	return create(eh, gateway, options);
}


std::unique_ptr<Samurai::IO::Net::UPnP::PortMapper>
Samurai::IO::Net::UPnP::PortMapper::create(PortMapperEventHandler* eh, Gateway* gateway,
                                           const Options& options)
{
	if (!gateway) return nullptr;
	return std::unique_ptr<PortMapper>(new PortMapper(eh, gateway, options));
}


Samurai::IO::Net::UPnP::PortMapper::PortMapper(PortMapperEventHandler* eh,
                                               Gateway* owner, const Options& opts)
	: eventHandler(eh), gateway(owner), options(opts)
{
}


/*
 * NOTE: nothing here touches the network. Deleting the mappings would mean
 * driving the event loop from a destructor, which re-enters arbitrary
 * application code during teardown and in an order nothing controls. The finite
 * lease is what cleans up after a process that goes away without saying so, and
 * shutdown() is what a caller uses when it wants to be tidy.
 */
Samurai::IO::Net::UPnP::PortMapper::~PortMapper()
{
	renewal.reset();
	requests.clear();
}


Samurai::IO::Net::InetAddress
Samurai::IO::Net::UPnP::PortMapper::internal_client(const Mapping& mapping) const
{
	if (mapping.internalClient.getType() != InetAddress::Version::Unspecified)
		return mapping.internalClient;

	/* Taken from the connection the description came over: it is the only thing
	   that knows which of this host's addresses reaches the gateway. */
	if (const InetAddress* local = gateway->getLocalAddress()) return *local;

	return InetAddress();
}


std::vector<Samurai::IO::Net::UPnP::Argument>
Samurai::IO::Net::UPnP::PortMapper::internal_addArguments(const Mapping& mapping,
                                                         bool anyPort) const
{
	/* Ordered as the specification orders them: more than one gateway reads its
	   arguments positionally and refuses a correct request in the wrong order. */
	std::vector<Argument> args;
	args.emplace_back("NewRemoteHost", mapping.remoteHost);
	args.emplace_back("NewExternalPort", std::to_string(mapping.externalPort));
	args.emplace_back("NewProtocol", toString(mapping.protocol));
	args.emplace_back("NewInternalPort", std::to_string(mapping.internalPort));
	args.emplace_back("NewInternalClient", internal_client(mapping).toString());
	args.emplace_back("NewEnabled", mapping.enabled ? "1" : "0");
	args.emplace_back("NewPortMappingDescription", mapping.description);
	args.emplace_back("NewLeaseDuration", std::to_string(mapping.lease.count()));

	(void) anyPort;
	return args;
}


void Samurai::IO::Net::UPnP::PortMapper::internal_start(std::unique_ptr<Request> request)
{
	Request* borrowed = request.get();
	requests.push_back(std::move(request));
	internal_issue(borrowed);
}


void Samurai::IO::Net::UPnP::PortMapper::internal_issue(Request* request)
{
	std::vector<Argument> args;
	const char* name = nullptr;

	switch (request->kind)
	{
		case Request::Kind::Add:
		case Request::Kind::Renew:
			name = "AddPortMapping";
			args = internal_addArguments(request->mapping, false);
			break;

		case Request::Kind::AddAny:
			/* AddAnyPortMapping is what the caller wants when it does not care
			   which external port it gets, and it exists only on version 2. */
			if (!request->triedAnyPort && gateway->supportsIgd2())
			{
				name = "AddAnyPortMapping";
				args = internal_addArguments(request->mapping, true);
			}
			else
			{
				name = "AddPortMapping";
				args = internal_addArguments(request->mapping, false);
			}
			break;

		case Request::Kind::Delete:
			name = "DeletePortMapping";
			args.emplace_back("NewRemoteHost", request->key.remoteHost);
			args.emplace_back("NewExternalPort", std::to_string(request->key.externalPort));
			args.emplace_back("NewProtocol", toString(request->key.protocol));
			break;

		case Request::Kind::Get:
			name = "GetSpecificPortMappingEntry";
			args.emplace_back("NewRemoteHost", request->key.remoteHost);
			args.emplace_back("NewExternalPort", std::to_string(request->key.externalPort));
			args.emplace_back("NewProtocol", toString(request->key.protocol));
			break;

		case Request::Kind::List:
			/* GetGenericPortMappingEntry rather than the version 2
			   GetListOfPortMappings: this one is implemented everywhere, and its
			   answer is arguments rather than an XML document nested inside a
			   SOAP string argument. */
			name = "GetGenericPortMappingEntry";
			args.emplace_back("NewPortMappingIndex", std::to_string(request->index));
			break;

		case Request::Kind::ExternalAddress:
			name = "GetExternalIPAddress";
			break;
	}

	ServiceKind kind = ServiceKind::WanIpConnection;
	if (!gateway->hasService(kind)) kind = ServiceKind::WanPppConnection;

	request->action = gateway->invoke(this, kind, name, args);
	if (!request->action)
	{
		internal_report(Error::NoService, DeviceError::Unknown, 0);
		internal_finish(request);
	}
}


void Samurai::IO::Net::UPnP::PortMapper::internal_finish(Request* request)
{
	const auto it = std::find_if(requests.begin(), requests.end(),
		[request](const std::unique_ptr<Request>& candidate)
		{ return candidate.get() == request; });

	if (it != requests.end()) requests.erase(it);
}


void Samurai::IO::Net::UPnP::PortMapper::internal_report(Error error, DeviceError device,
                                                         uint16_t code)
{
	if (eventHandler) eventHandler->EventMapperError(this, error, device, code);
}


void Samurai::IO::Net::UPnP::PortMapper::addMapping(const Mapping& mapping)
{
	auto request = std::make_unique<Request>();
	request->kind = Request::Kind::Add;
	request->mapping = mapping;

	/* Already known to refuse one, so the round trip that would find out again
	   is skipped. */
	if (gateway->getPermanentLeasesOnly()) request->mapping.lease = std::chrono::seconds(0);
	else request->mapping.lease = options.lease;

	if (mapping.lease.count()) request->mapping.lease = mapping.lease;
	if (gateway->getPermanentLeasesOnly()) request->mapping.lease = std::chrono::seconds(0);

	if (!request->mapping.externalPort)
		request->mapping.externalPort = request->mapping.internalPort;

	/* Resolved once, here, so that what is reported back and what is sent on a
	   renewal both name the same client rather than resolving it again later. */
	request->mapping.internalClient = internal_client(request->mapping);

	internal_start(std::move(request));
}


void Samurai::IO::Net::UPnP::PortMapper::addAnyMapping(const Mapping& mapping)
{
	auto request = std::make_unique<Request>();
	request->kind = Request::Kind::AddAny;
	request->mapping = mapping;

	if (gateway->getPermanentLeasesOnly()) request->mapping.lease = std::chrono::seconds(0);
	else if (!mapping.lease.count()) request->mapping.lease = options.lease;

	if (!request->mapping.externalPort)
		request->mapping.externalPort = request->mapping.internalPort;

	request->mapping.internalClient = internal_client(request->mapping);

	internal_start(std::move(request));
}


void Samurai::IO::Net::UPnP::PortMapper::deleteMapping(const MappingKey& key)
{
	auto request = std::make_unique<Request>();
	request->kind = Request::Kind::Delete;
	request->key = key;
	internal_start(std::move(request));
}


void Samurai::IO::Net::UPnP::PortMapper::getMapping(const MappingKey& key)
{
	auto request = std::make_unique<Request>();
	request->kind = Request::Kind::Get;
	request->key = key;
	internal_start(std::move(request));
}


void Samurai::IO::Net::UPnP::PortMapper::listMappings(size_t maxEntries)
{
	auto request = std::make_unique<Request>();
	request->kind = Request::Kind::List;
	request->index = 0;
	request->maxEntries = maxEntries;
	internal_start(std::move(request));
}


void Samurai::IO::Net::UPnP::PortMapper::getExternalAddress()
{
	auto request = std::make_unique<Request>();
	request->kind = Request::Kind::ExternalAddress;
	internal_start(std::move(request));
}


void Samurai::IO::Net::UPnP::PortMapper::shutdown()
{
	shutting_down = true;
	renewal.reset();

	/* Copied first: deleteMapping() appends to 'requests', and held shrinks as
	   the answers arrive. */
	const std::vector<Mapping> doomed = held;
	for (const Mapping& mapping : doomed) deleteMapping(mapping.key());
}


void Samurai::IO::Net::UPnP::PortMapper::cancel()
{
	renewal.reset();
	requests.clear();
}


Samurai::IO::Net::UPnP::Mapping*
Samurai::IO::Net::UPnP::PortMapper::internal_find(const MappingKey& key)
{
	for (Mapping& mapping : held)
		if (mapping.key() == key) return &mapping;

	return nullptr;
}


void Samurai::IO::Net::UPnP::PortMapper::internal_forget(const MappingKey& key)
{
	const auto it = std::find_if(held.begin(), held.end(),
		[&key](const Mapping& mapping) { return mapping.key() == key; });

	if (it != held.end()) held.erase(it);
}


/*
 * One timer for the whole set, armed for the soonest renewal. Finding which
 * entry is due by scanning is what keeps a timer per mapping - and the
 * bookkeeping that goes with it - out of this.
 */
void Samurai::IO::Net::UPnP::PortMapper::internal_scheduleRenewal(const Mapping& mapping)
{
	if (!options.renewAutomatically || shutting_down) return;

	std::chrono::milliseconds when{ 0 };

	if (mapping.lease.count())
	{
		const double fraction = (options.renewAt > 0.0 && options.renewAt < 1.0)
			? options.renewAt : 0.5;
		const double seconds = (double) mapping.lease.count() * fraction;
		/* At least a second, so a very short lease cannot make this spin. */
		when = std::chrono::milliseconds((long long) (seconds * 1000.0));
		if (when < std::chrono::milliseconds(1000)) when = std::chrono::milliseconds(1000);
	}
	else
	{
		/* A permanent mapping is re-added anyway: gateways reboot, and a
		   permanent entry does not survive that. */
		if (!options.permanentRefresh.count()) return;
		when = std::chrono::duration_cast<std::chrono::milliseconds>(
			options.permanentRefresh);
	}

	/* Only shortened, never lengthened: another mapping may already be due
	   sooner. */
	if (renewal && renewal->deadline() <= Samurai::Timer::clock::now() + when) return;

	renewal = std::make_unique<Samurai::Timer>(this, when, true);
}


void Samurai::IO::Net::UPnP::PortMapper::EventTimeout(Samurai::Timer*)
{
	renewal.reset();
	if (shutting_down) return;

	/* Re-adding an identical mapping is how a gateway is told to refresh one. */
	const std::vector<Mapping> due = held;
	for (const Mapping& mapping : due)
	{
		auto request = std::make_unique<Request>();
		request->kind = Request::Kind::Renew;
		request->mapping = mapping;
		internal_start(std::move(request));
	}
}


void Samurai::IO::Net::UPnP::PortMapper::EventActionResponse(Action* action,
                                                             const ActionResult& result)
{
	Request* request = nullptr;
	for (const std::unique_ptr<Request>& candidate : requests)
		if (candidate->action.get() == action) { request = candidate.get(); break; }

	if (!request) return;

	switch (request->kind)
	{
		case Request::Kind::Add:
		case Request::Kind::AddAny:
		case Request::Kind::Renew:
		{
			Mapping granted = request->mapping;

			/* AddAnyPortMapping answers with the port it chose, which need not be
			   the one that was asked for. */
			if (const std::string* reserved = result.find("NewReservedPort"))
			{
				const uint64_t port = Samurai::Util::Convert::to_uint64(*reserved);
				if (port && port <= 0xffff) granted.externalPort = (uint16_t) port;
			}

			const bool renewing = (request->kind == Request::Kind::Renew);
			const MappingKey key = granted.key();

			if (Mapping* existing = internal_find(key)) *existing = granted;
			else held.push_back(granted);

			internal_scheduleRenewal(granted);

			const Request::Kind kind = request->kind;
			internal_finish(request);

			if (!eventHandler) return;
			if (renewing) eventHandler->EventMappingRenewed(this, granted);
			else eventHandler->EventMappingAdded(this, granted);
			(void) kind;
			return;
		}

		case Request::Kind::Delete:
		{
			const MappingKey key = request->key;
			internal_forget(key);
			internal_finish(request);

			if (eventHandler) eventHandler->EventMappingRemoved(this, key);
			return;
		}

		case Request::Kind::Get:
		{
			Mapping found;
			found.protocol = request->key.protocol;
			found.externalPort = request->key.externalPort;
			found.remoteHost = request->key.remoteHost;
			found.description = result.value("NewPortMappingDescription");
			found.internalClient = InetAddress(result.value("NewInternalClient"));
			found.enabled = result.value("NewEnabled") != "0";

			const uint64_t internal =
				Samurai::Util::Convert::to_uint64(result.value("NewInternalPort"));
			if (internal <= 0xffff) found.internalPort = (uint16_t) internal;

			found.lease = std::chrono::seconds(
				(long long) Samurai::Util::Convert::to_uint64(
					result.value("NewLeaseDuration")));

			internal_finish(request);
			if (eventHandler) eventHandler->EventMappingFound(this, found);
			return;
		}

		case Request::Kind::List:
		{
			Mapping entry;
			entry.remoteHost = result.value("NewRemoteHost");
			entry.description = result.value("NewPortMappingDescription");
			entry.internalClient = InetAddress(result.value("NewInternalClient"));
			entry.enabled = result.value("NewEnabled") != "0";
			entry.protocol = Samurai::Util::iequals(result.value("NewProtocol"), "UDP")
				? Protocol::UDP : Protocol::TCP;

			const uint64_t external =
				Samurai::Util::Convert::to_uint64(result.value("NewExternalPort"));
			if (external <= 0xffff) entry.externalPort = (uint16_t) external;

			const uint64_t internal =
				Samurai::Util::Convert::to_uint64(result.value("NewInternalPort"));
			if (internal <= 0xffff) entry.internalPort = (uint16_t) internal;

			entry.lease = std::chrono::seconds(
				(long long) Samurai::Util::Convert::to_uint64(
					result.value("NewLeaseDuration")));

			request->collected.push_back(entry);
			request->index++;

			if (request->collected.size() >= request->maxEntries)
			{
				const std::vector<Mapping> collected = request->collected;
				internal_finish(request);
				if (eventHandler) eventHandler->EventMappingList(this, collected);
				return;
			}

			/* Keep walking; the end arrives as error 713. */
			internal_issue(request);
			return;
		}

		case Request::Kind::ExternalAddress:
		{
			const InetAddress address(result.value("NewExternalIPAddress"));
			internal_finish(request);
			if (eventHandler) eventHandler->EventExternalAddress(this, address);
			return;
		}
	}
}


void Samurai::IO::Net::UPnP::PortMapper::EventActionFault(Action* action,
                                                          DeviceError error,
                                                          uint16_t code,
                                                          std::string_view)
{
	Request* request = nullptr;
	for (const std::unique_ptr<Request>& candidate : requests)
		if (candidate->action.get() == action) { request = candidate.get(); break; }

	if (!request) return;

	const bool adding = request->kind == Request::Kind::Add
		|| request->kind == Request::Kind::AddAny
		|| request->kind == Request::Kind::Renew;

	/*
	 * Many gateways refuse any lease at all. Asking again for a permanent
	 * mapping is what the caller wanted, and the flag on the gateway means the
	 * next add skips the wasted round trip. Guarded so it cannot loop.
	 */
	if (adding && !request->triedPermanent && options.fallBackToPermanent &&
	    request->mapping.lease.count() &&
	    (error == DeviceError::OnlyPermanentLeasesSupported ||
	     error == DeviceError::InvalidArgs))
	{
		request->triedPermanent = true;
		request->mapping.lease = std::chrono::seconds(0);
		gateway->setPermanentLeasesOnly(true);
		internal_issue(request);
		return;
	}

	/* AddAnyPortMapping is optional; a device without it says so, and the
	   version 1 action is then tried instead. */
	if (request->kind == Request::Kind::AddAny && !request->triedAnyPort &&
	    (error == DeviceError::InvalidAction ||
	     error == DeviceError::OptionalActionNotImplemented))
	{
		request->triedAnyPort = true;
		internal_issue(request);
		return;
	}

	/* Someone else holds the port. Walking to the next one is what
	   addAnyMapping() promises; a plain addMapping() asked for one port and is
	   told it cannot have it. */
	if (request->kind == Request::Kind::AddAny &&
	    error == DeviceError::ConflictInMappingEntry &&
	    request->attempts + 1 < options.conflictRetries)
	{
		request->attempts++;
		if (request->mapping.externalPort < 0xffff)
		{
			request->mapping.externalPort++;
			internal_issue(request);
			return;
		}
	}

	/* Walking the table ends here rather than failing: 713 is how a gateway
	   says the index is past the last entry. */
	if (request->kind == Request::Kind::List &&
	    (error == DeviceError::SpecifiedArrayIndexInvalid ||
	     error == DeviceError::NoSuchEntryInArray))
	{
		const std::vector<Mapping> collected = request->collected;
		internal_finish(request);
		if (eventHandler) eventHandler->EventMappingList(this, collected);
		return;
	}

	/*
	 * A renewal refused because another host took the port is reported and then
	 * left alone: re-mapping would take a port that is now someone else's.
	 */
	if (request->kind == Request::Kind::Renew &&
	    error == DeviceError::ConflictInMappingEntry)
	{
		internal_forget(request->mapping.key());
	}

	internal_finish(request);
	internal_report(Error::Device, error, code);
}


void Samurai::IO::Net::UPnP::PortMapper::EventActionError(Action* action, Error error)
{
	Request* request = nullptr;
	for (const std::unique_ptr<Request>& candidate : requests)
		if (candidate->action.get() == action) { request = candidate.get(); break; }

	if (!request) return;

	internal_finish(request);
	internal_report(error, DeviceError::Unknown, 0);
}
