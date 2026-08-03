/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/upnp/gateway.h>
#include <samurai/os.h>
#include <samurai/util/string.h>

#include "description.h"
#include "soap.h"
#include "ssdp.h"

#include <algorithm>

/** What the gateway carries while searching, and once a description is read. */
struct Samurai::IO::Net::UPnP::Gateway::Discovered
	: public Samurai::IO::Net::UPnP::Ssdp::SearchEventHandler
{
	DeviceDescription description;
	std::unique_ptr<Ssdp::Search> search;
	/** Replies still to be tried, in the order they arrived. */
	std::vector<Ssdp::Reply> candidates;
	size_t next_candidate = 0;

	Gateway* owner = nullptr;

	void EventSsdpReply(const Ssdp::Reply& reply) override
	{
		candidates.push_back(reply);
		if (!owner) return;

		GatewayEventHandler::Candidate candidate;
		candidate.location = reply.location;
		candidate.st = reply.st;
		candidate.usn = reply.usn;
		candidate.server = reply.server;
		candidate.source = reply.source;

		owner->internal_candidate(candidate);
	}

	void EventSsdpFinished() override
	{
		if (owner) owner->internal_searchFinished();
	}
};


const std::string*
Samurai::IO::Net::UPnP::ActionResult::find(std::string_view name) const
{
	for (const Argument& argument : arguments)
		if (argument.first == name) return &argument.second;

	return nullptr;
}


std::string Samurai::IO::Net::UPnP::ActionResult::value(std::string_view name) const
{
	const std::string* found = find(name);
	return found ? *found : std::string();
}


bool Samurai::IO::Net::UPnP::GatewayInfo::isValid() const
{
	return descriptionURL.isValid() && controlURL.isValid() && !serviceType.empty();
}


bool Samurai::IO::Net::UPnP::GatewayInfo::hasExpired() const
{
	if (expires == Samurai::Timer::clock::time_point()) return false;
	return Samurai::Timer::clock::now() >= expires;
}


Samurai::IO::Net::UPnP::Action::Action(Gateway* owner, ActionEventHandler* eh,
                                       ServiceKind service, std::string_view name,
                                       std::span<const Argument> arguments)
	: gateway(owner)
	, eventHandler(eh)
	, kind(service)
	, action(name)
	, args(arguments.begin(), arguments.end())
{
}


Samurai::IO::Net::UPnP::Action::~Action()
{
	if (gateway) gateway->internal_cancelAction(this);
}


Samurai::IO::Net::UPnP::Gateway::Gateway(GatewayEventHandler* eh, const Options& opts)
	: eventHandler(eh)
	, options(opts)
	, discovered(std::make_unique<Discovered>())
{
}


Samurai::IO::Net::UPnP::Gateway::~Gateway()
{
	/* Each Action holds a pointer back here and clears it through
	   internal_cancelAction(); one outliving this must not follow a dangling
	   one. */
	for (Action* action : pending) action->gateway = nullptr;
	if (inflight) inflight->gateway = nullptr;

	pending.clear();
	inflight = nullptr;
	http.reset();
	deferred.reset();
}


std::unique_ptr<Samurai::IO::Net::UPnP::Gateway>
Samurai::IO::Net::UPnP::Gateway::discover(GatewayEventHandler* eh)
{
	const Options options;
	return discover(eh, options);
}


std::unique_ptr<Samurai::IO::Net::UPnP::Gateway>
Samurai::IO::Net::UPnP::Gateway::discover(GatewayEventHandler* eh,
                                          const Options& options)
{
	std::unique_ptr<Gateway> self(new Gateway(eh, options));

	Ssdp::Search::Options search;
	search.timeout = options.searchTimeout;
	search.interval = options.searchInterval;
	search.mx = options.mx;
	search.rounds = options.searchRounds;
	search.hops = options.multicastHops;
	search.searchIPv4 = options.searchIPv4;
	search.searchIPv6 = options.searchIPv6;
	search.requireLocationMatchesSource = options.requireLocationMatchesSource;

	self->discovered->owner = self.get();
	self->discovered->search = Ssdp::Search::start(self->discovered.get(), search);

	if (!self->discovered->search)
	{
		/* Not one interface could be searched, which is the only outright
		   failure: a host where some refuse still searches the rest. */
		self->internal_reportLater(Error::NoGateway);
		return self;
	}

	self->state = State::Searching;
	return self;
}


std::unique_ptr<Samurai::IO::Net::UPnP::Gateway>
Samurai::IO::Net::UPnP::Gateway::describe(GatewayEventHandler* eh, const URL& location)
{
	const Options options;
	return describe(eh, location, options);
}


std::unique_ptr<Samurai::IO::Net::UPnP::Gateway>
Samurai::IO::Net::UPnP::Gateway::attach(GatewayEventHandler* eh, const GatewayInfo& info)
{
	const Options options;
	return attach(eh, info, options);
}


std::unique_ptr<Samurai::IO::Net::UPnP::Gateway>
Samurai::IO::Net::UPnP::Gateway::describe(GatewayEventHandler* eh, const URL& location,
                                          const Options& options)
{
	std::unique_ptr<Gateway> self(new Gateway(eh, options));

	if (!location.isValid() || location.getHostname().empty())
	{
		self->internal_reportLater(Error::DescriptionFailed);
		return self;
	}

	self->internal_startDescription(location);
	return self;
}


std::unique_ptr<Samurai::IO::Net::UPnP::Gateway>
Samurai::IO::Net::UPnP::Gateway::attach(GatewayEventHandler* eh, const GatewayInfo& info,
                                        const Options& options)
{
	std::unique_ptr<Gateway> self(new Gateway(eh, options));

	if (!info.isValid())
	{
		self->internal_reportLater(Error::DescriptionFailed);
		return self;
	}

	Service service;
	service.serviceType = info.serviceType;
	service.controlURL = info.controlURL;

	ServiceKind kind = ServiceKind::WanIpConnection;
	int version = 1;
	if (!Description::classify(info.serviceType, kind, version))
	{
		self->internal_reportLater(Error::NoService);
		return self;
	}

	service.kind = kind;
	service.version = version;

	self->description_url = info.descriptionURL;
	self->friendly_name = info.friendlyName;
	self->usn = info.usn;
	self->device_version = info.deviceVersion;
	self->permanent_leases_only = info.permanentLeasesOnly;
	self->local_address = info.localAddress;
	self->expires = info.expires;
	self->discovered->description.services.push_back(service);

	/* Reported on the next turn of the loop, not from inside this: the caller
	   has not stored the pointer yet, so it could neither correlate the event
	   nor safely destroy what it is being told about. */
	self->state = State::Ready;
	self->internal_reportLater(Error::None);
	return self;
}


Samurai::IO::Net::UPnP::GatewayInfo Samurai::IO::Net::UPnP::Gateway::info() const
{
	GatewayInfo out;
	out.descriptionURL = description_url;
	out.friendlyName = friendly_name;
	out.usn = usn;
	out.deviceVersion = device_version;
	out.permanentLeasesOnly = permanent_leases_only;
	out.localAddress = local_address;
	out.expires = expires;

	if (const Service* service = discovered->description.preferredConnection())
	{
		out.controlURL = service->controlURL;
		out.serviceType = service->serviceType;
	}

	return out;
}


bool Samurai::IO::Net::UPnP::Gateway::hasService(ServiceKind kind) const
{
	return discovered->description.find(kind) != nullptr;
}


std::string Samurai::IO::Net::UPnP::Gateway::getServiceType(ServiceKind kind) const
{
	const Service* service = discovered->description.find(kind);
	return service ? service->serviceType : std::string();
}


const Samurai::IO::Net::InetAddress*
Samurai::IO::Net::UPnP::Gateway::getLocalAddress() const
{
	return local_address.getType() == InetAddress::Version::Unspecified
		? nullptr : &local_address;
}


void Samurai::IO::Net::UPnP::Gateway::internal_reportLater(Error error)
{
	deferred_error = error;
	deferred = std::make_unique<Samurai::Timer>(this, std::chrono::milliseconds(0), true);
}


void Samurai::IO::Net::UPnP::Gateway::internal_startDescription(const URL& location)
{
	description_url = location;
	state = State::FetchingDescription;

	Samurai::IO::Net::HTTP::Client::Options http_options;
	http_options.timeout = options.httpTimeout;
	http_options.limits.maxBody = options.maxDescriptionBytes;
	http_options.userAgent = std::string(Samurai::OS::getName()) + "/"
		+ Samurai::OS::getVersion() + " UPnP/1.1 Samurai/1.0";

	http = std::make_unique<Samurai::IO::Net::HTTP::Client>(this, http_options);
	http->get(location);
}


/*
 * A reply arrived. The description of the first one is fetched at once rather
 * than waiting out the search window, so a gateway that answers quickly costs
 * one round trip and not the whole timeout. Later replies keep queueing behind
 * it, and are tried in turn if this one is not a gateway after all.
 */
void Samurai::IO::Net::UPnP::Gateway::internal_candidate(
	const GatewayEventHandler::Candidate& candidate)
{
	if (eventHandler) eventHandler->EventGatewayCandidate(this, candidate);

	if (state != State::Searching) return;
	internal_tryNextCandidate();
}


bool Samurai::IO::Net::UPnP::Gateway::internal_tryNextCandidate()
{
	if (discovered->next_candidate >= discovered->candidates.size()) return false;

	const Ssdp::Reply next = discovered->candidates[discovered->next_candidate++];
	internal_startDescription(next.location);
	return true;
}


void Samurai::IO::Net::UPnP::Gateway::internal_searchFinished()
{
	/* Only meaningful while nothing has been chosen: a description already being
	   fetched carries on, and the queue is what decides the outcome. */
	if (state != State::Searching) return;

	if (internal_tryNextCandidate()) return;

	internal_fail(Error::NoGateway);
}


void Samurai::IO::Net::UPnP::Gateway::internal_ready()
{
	state = State::Ready;

	if (!reported)
	{
		reported = true;
		if (eventHandler) eventHandler->EventGatewayFound(this);
	}

	internal_pump();
}


void Samurai::IO::Net::UPnP::Gateway::internal_fail(Error error)
{
	state = State::Failed;

	/* Anything queued can never run now, so it is told rather than left to
	   wait for an event that will not come. */
	std::vector<Action*> doomed = pending;
	pending.clear();

	if (inflight)
	{
		doomed.insert(doomed.begin(), inflight);
		inflight = nullptr;
	}

	http.reset();

	if (!reported)
	{
		reported = true;
		if (eventHandler) eventHandler->EventGatewayError(this, error);
	}

	for (Action* action : doomed)
	{
		action->queued = false;
		if (action->eventHandler) action->eventHandler->EventActionError(action, error);
	}
}


void Samurai::IO::Net::UPnP::Gateway::cancel()
{
	if (state == State::Failed) return;
	internal_fail(Error::Cancelled);
}


std::unique_ptr<Samurai::IO::Net::UPnP::Action>
Samurai::IO::Net::UPnP::Gateway::invoke(ActionEventHandler* eh, ServiceKind kind,
                                        std::string_view action,
                                        std::span<const Argument> arguments)
{
	if (!discovered->description.find(kind)) return nullptr;

	std::unique_ptr<Action> queued(new Action(this, eh, kind, action, arguments));
	pending.push_back(queued.get());

	if (state == State::Ready) internal_pump();
	return queued;
}


void Samurai::IO::Net::UPnP::Gateway::internal_cancelAction(Action* action)
{
	const auto it = std::find(pending.begin(), pending.end(), action);
	if (it != pending.end()) pending.erase(it);

	if (inflight == action)
	{
		inflight = nullptr;
		/* The reply is no longer wanted, and the connection carries only this
		   one request. */
		if (http) http->abort();
		internal_pump();
	}
}


/*
 * One action at a time. An embedded HTTP server commonly accepts a single
 * connection and drops a concurrent second request rather than refusing it, so
 * overlapping them loses answers rather than reporting an error.
 */
void Samurai::IO::Net::UPnP::Gateway::internal_pump()
{
	if (state != State::Ready || inflight || pending.empty()) return;

	Action* next = pending.front();
	pending.erase(pending.begin());
	inflight = next;
	internal_sendAction(next);
}


void Samurai::IO::Net::UPnP::Gateway::internal_sendAction(Action* action)
{
	const Service* service = discovered->description.find(action->kind);
	if (!service)
	{
		inflight = nullptr;
		action->queued = false;
		if (action->eventHandler)
			action->eventHandler->EventActionError(action, Error::NoService);
		internal_pump();
		return;
	}

	const std::string body =
		Soap::buildRequest(service->serviceType, action->action, action->args);

	Samurai::IO::Net::HTTP::Headers extra;
	extra.set("Content-Type", "text/xml; charset=\"utf-8\"");
	/* Spelled as the specification spells it, and sent with that case: more
	   than one gateway matches the field name case-sensitively. */
	extra.set("SOAPAction", Soap::buildActionHeader(service->serviceType, action->action));

	Samurai::IO::Net::HTTP::Client::Options http_options;
	http_options.timeout = options.httpTimeout;
	http_options.userAgent = std::string(Samurai::OS::getName()) + "/"
		+ Samurai::OS::getVersion() + " UPnP/1.1 Samurai/1.0";

	http = std::make_unique<Samurai::IO::Net::HTTP::Client>(this, http_options);
	http->request(Samurai::IO::Net::HTTP::Method::Post, service->controlURL, extra, body);
}


void Samurai::IO::Net::UPnP::Gateway::internal_finishAction(Action* action)
{
	if (inflight == action) inflight = nullptr;
	action->queued = false;
	internal_pump();
}


void Samurai::IO::Net::UPnP::Gateway::EventHttpResponse(
	Samurai::IO::Net::HTTP::Client* client,
	const Samurai::IO::Net::HTTP::Response& response)
{
	if (state == State::FetchingDescription)
	{
		if (!response.isSuccess()) { internal_fail(Error::DescriptionFailed); return; }

		/* Taken from the connection the description came over, which is the one
		   thing that knows which local address reaches this gateway. */
		if (const InetAddress* local = client->getLocalAddress())
			local_address = *local;

		const Error parsed = Description::parse(response.getBody(), description_url,
		                                        discovered->description);
		if (parsed != Error::None) { internal_fail(parsed); return; }

		friendly_name = discovered->description.friendlyName;
		device_version = discovered->description.deviceVersion;

		http.reset();
		internal_ready();
		return;
	}

	Action* action = inflight;
	if (!action) return;

	ActionResult result;
	std::optional<ActionFault> fault;
	const Error status = Soap::parseResponse(response.getBody(), result, fault);

	/* Copied out before the handler runs: it may queue another action, which
	   replaces the HTTP client this was delivered through. */
	internal_finishAction(action);

	if (!action->eventHandler) return;

	if (status == Error::Device && fault.has_value())
	{
		action->eventHandler->EventActionFault(action, fault->error, fault->code,
		                                       fault->description);
		return;
	}

	if (status != Error::None)
	{
		action->eventHandler->EventActionError(action, status);
		return;
	}

	/*
	 * A response is only an answer if the request succeeded. A 4xx or 5xx with
	 * no readable fault is the device refusing without saying why.
	 */
	if (!response.isSuccess())
	{
		action->eventHandler->EventActionError(action, Error::BadResponse);
		return;
	}

	action->eventHandler->EventActionResponse(action, result);
}


void Samurai::IO::Net::UPnP::Gateway::EventHttpError(
	Samurai::IO::Net::HTTP::Client*, Samurai::IO::Net::HTTP::Status, const char*)
{
	if (state == State::FetchingDescription)
	{
		/* The next reply the search produced may be the real gateway, so the
		   queue is only exhausted once there is nothing left to try. */
		if (internal_tryNextCandidate()) return;

		/* Nothing queued, but the search may still be running and about to
		   produce something: go back to waiting rather than giving up on a
		   gateway that has not answered yet. */
		if (discovered->search && !discovered->search->isFinished())
		{
			state = State::Searching;
			return;
		}

		internal_fail(Error::DescriptionFailed);
		return;
	}

	Action* action = inflight;
	if (!action) return;

	internal_finishAction(action);

	if (action->eventHandler)
		action->eventHandler->EventActionError(action, Error::Network);
}


/*
 * NOTE: the timer is released before anything is reported. A handler is entitled
 * to destroy this gateway, and a timer callback - unlike a monitor dispatch -
 * holds nothing that would keep it alive.
 */
void Samurai::IO::Net::UPnP::Gateway::EventTimeout(Samurai::Timer*)
{
	deferred.reset();

	const Error error = deferred_error;
	deferred_error = Error::None;

	if (error == Error::None) internal_ready();
	else internal_fail(error);
}
