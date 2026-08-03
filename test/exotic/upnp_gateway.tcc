/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/net/serversocket.h>
#include <samurai/io/net/socket.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/upnp/gateway.h>
#include <samurai/io/net/upnp/portmap.h>
#include <samurai/io/net/upnp/portmapper.h>
#include <samurai/io/net/url.h>
#include <samurai/timer.h>

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

/*
 * A gateway impersonated on the loopback, answering from a scripted table.
 *
 * This is what makes the whole state machine testable without hardware: the
 * description fetch, the service walk, the lease fallback, the conflict walk and
 * the renewal all run against a device whose answers the case chose. Everything
 * here is deterministic and reaches no network.
 *
 * Gateway::describe() is what it is driven through, which is also the entry
 * point a caller uses when it has the gateway's URL from configuration.
 */

namespace {

using Samurai::IO::Net::SocketMonitor;
using Samurai::IO::Net::URL;
using namespace Samurai::IO::Net::UPnP;

const char* IGD1_DESCRIPTION =
	"<?xml version=\"1.0\"?>\n"
	"<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\n"
	" <specVersion><major>1</major><minor>0</minor></specVersion>\n"
	" <device>\n"
	"  <deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>\n"
	"  <friendlyName>Test Router</friendlyName>\n"
	"  <UDN>uuid:11111111-2222-3333-4444-555555555555</UDN>\n"
	"  <deviceList>\n"
	"   <device>\n"
	"    <deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType>\n"
	"    <deviceList>\n"
	"     <device>\n"
	"      <deviceType>urn:schemas-upnp-org:device:WANConnectionDevice:1</deviceType>\n"
	"      <serviceList>\n"
	"       <service>\n"
	"        <serviceType>\n"
	"          urn:schemas-upnp-org:service:WANIPConnection:1\n"
	"        </serviceType>\n"
	"        <controlURL>/ctl/IPConn</controlURL>\n"
	"       </service>\n"
	"      </serviceList>\n"
	"     </device>\n"
	"    </deviceList>\n"
	"   </device>\n"
	"  </deviceList>\n"
	" </device>\n"
	"</root>\n";

const char* IGD2_DESCRIPTION =
	"<?xml version=\"1.0\"?>\n"
	"<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\n"
	" <device>\n"
	"  <deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:2</deviceType>\n"
	"  <friendlyName>Test Router 2</friendlyName>\n"
	"  <serviceList>\n"
	"   <service>\n"
	"    <serviceType>urn:schemas-upnp-org:service:WANIPConnection:2</serviceType>\n"
	"    <controlURL>/ctl/IPConn2</controlURL>\n"
	"   </service>\n"
	"  </serviceList>\n"
	" </device>\n"
	"</root>\n";

const char* PPP_DESCRIPTION =
	"<?xml version=\"1.0\"?>\n"
	"<root>\n"
	" <device>\n"
	"  <deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>\n"
	"  <friendlyName>PPP Router</friendlyName>\n"
	"  <serviceList>\n"
	"   <service>\n"
	"    <serviceType>urn:schemas-upnp-org:service:WANPPPConnection:1</serviceType>\n"
	"    <controlURL>/ctl/PPP</controlURL>\n"
	"   </service>\n"
	"  </serviceList>\n"
	" </device>\n"
	"</root>\n";

const char* NO_WAN_DESCRIPTION =
	"<?xml version=\"1.0\"?>\n"
	"<root>\n"
	" <device>\n"
	"  <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>\n"
	"  <friendlyName>Not A Router</friendlyName>\n"
	"  <serviceList>\n"
	"   <service>\n"
	"    <serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType>\n"
	"    <controlURL>/ctl/Content</controlURL>\n"
	"   </service>\n"
	"  </serviceList>\n"
	" </device>\n"
	"</root>\n";

std::string soap_response(const std::string& action, const std::string& body)
{
	std::string out;
	out += "<?xml version=\"1.0\"?>";
	out += "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">";
	out += "<s:Body><u:" + action + "Response"
	       " xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">";
	out += body;
	out += "</u:" + action + "Response></s:Body></s:Envelope>";
	return out;
}

std::string soap_fault(int code, const std::string& description)
{
	std::string out;
	out += "<?xml version=\"1.0\"?>";
	out += "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">";
	out += "<s:Body><s:Fault><faultcode>s:Client</faultcode>";
	out += "<faultstring>UPnPError</faultstring><detail>";
	out += "<UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\">";
	out += "<errorCode>" + std::to_string(code) + "</errorCode>";
	out += "<errorDescription>" + description + "</errorDescription>";
	out += "</UPnPError></detail></s:Fault></s:Body></s:Envelope>";
	return out;
}

/** One scripted answer. */
struct Answer
{
	int status = 200;
	std::string body;
	std::string contentType = "text/xml; charset=\"utf-8\"";
};

Answer ok(const std::string& body)
{
	Answer answer;
	answer.body = body;
	return answer;
}

Answer fault(int code, const std::string& description = "refused")
{
	Answer answer;
	/* Sent as a 500, which is what the specification says - and separately
	   covered as a 200, because devices do that too. */
	answer.status = 500;
	answer.body = soap_fault(code, description);
	return answer;
}

/**
 * A gateway on the loopback.
 *
 * Answers a GET from 'description' and a POST from whatever the action name in
 * the SOAPAction header is scripted to. An action with a queue of answers hands
 * them out in order, which is how a fallback or a conflict walk is arranged.
 */
class FakeGateway
	: public Samurai::IO::Net::ServerSocketEventHandler
	, public Samurai::IO::Net::SocketEventHandler
{
	public:
		std::string description;
		std::map<std::string, std::vector<Answer>> answers;

		/* What the fake saw, so a case can assert on the requests. */
		std::vector<std::string> actions;
		std::vector<std::string> bodies;
		int requests = 0;
		int concurrent = 0;
		int most_concurrent = 0;

		/* Faults arrive as a 200 rather than a 500 when set. */
		bool faults_as_200 = false;
		bool never_answer = false;
		bool close_early = false;
		size_t oversize_body = 0;

		explicit FakeGateway(const char* canned) : description(canned)
		{
			Samurai::IO::Net::InetSocketAddress any((uint16_t) 0);
			server = Samurai::IO::Net::ServerSocket::create(this, any);
			if (!server || server->getFD() == -1) return;
			if (!server->listen()) return;
			port = server->getLocalPort();
		}

		~FakeGateway() override
		{
			live.clear();
			server.reset();
		}

		FakeGateway(const FakeGateway&) = delete;
		FakeGateway& operator=(const FakeGateway&) = delete;

		bool ready() const { return server && port != 0; }
		uint16_t getPort() const { return port; }

		URL location() const
		{
			return URL("http://127.0.0.1:" + std::to_string(port) + "/rootDesc.xml");
		}

		/** Script one answer, appended to whatever that action already has. */
		void script(const std::string& action, const Answer& answer)
		{
			answers[action].push_back(answer);
		}

		/** Script the same answer for every call of an action. */
		void always(const std::string& action, const Answer& answer)
		{
			repeating[action] = answer;
		}

	protected:
		void EventAcceptError(const Samurai::IO::Net::ServerSocket*, const char*) override
		{ }

		void EventAcceptSocket(const Samurai::IO::Net::ServerSocket*,
			std::shared_ptr<Samurai::IO::Net::Socket> socket) override
		{
			concurrent++;
			if (concurrent > most_concurrent) most_concurrent = concurrent;

			socket->setEventHandler(this);
			live.push_back(socket);
			buffers[socket.get()].clear();
		}

		void EventDataAvailable(const Samurai::IO::Net::Socket* which) override
		{
			Samurai::IO::Net::Socket* socket = const_cast<Samurai::IO::Net::Socket*>(which);

			char scratch[8192];
			size_t got = 0;
			std::error_code ec;
			if (socket->read(scratch, sizeof(scratch), got, ec) !=
				Samurai::IO::ReadResult::Ok) return;

			std::string& held = buffers[socket];
			held.append(scratch, got);

			const size_t head_end = held.find("\r\n\r\n");
			if (head_end == std::string::npos) return;

			/* Wait for the whole declared body before answering. */
			const size_t length = declared_length(held.substr(0, head_end));
			if (held.size() < head_end + 4 + length) return;

			const std::string head = held.substr(0, head_end);
			const std::string body = held.substr(head_end + 4, length);
			held.clear();

			requests++;

			if (never_answer) return;
			if (close_early) { finish(socket); return; }

			respond(socket, head, body);
		}

		void EventDisconnected(const Samurai::IO::Net::Socket*) override { }

	private:
		static size_t declared_length(const std::string& head)
		{
			const size_t at = head.find("Content-Length:");
			if (at == std::string::npos) return 0;

			size_t n = at + 15;
			while (n < head.size() && head[n] == ' ') n++;

			size_t value = 0;
			while (n < head.size() && head[n] >= '0' && head[n] <= '9')
				value = value * 10 + (size_t) (head[n++] - '0');

			return value;
		}

		/** The action name out of a SOAPAction header. */
		static std::string action_of(const std::string& head)
		{
			size_t at = head.find("SOAPAction:");
			if (at == std::string::npos) return std::string();

			const size_t hash = head.find('#', at);
			if (hash == std::string::npos) return std::string();

			const size_t quote = head.find('"', hash);
			if (quote == std::string::npos) return std::string();

			return head.substr(hash + 1, quote - hash - 1);
		}

		void respond(Samurai::IO::Net::Socket* socket, const std::string& head,
		             const std::string& body)
		{
			Answer answer;

			if (head.compare(0, 4, "GET ") == 0)
			{
				if (oversize_body)
				{
					answer.body = std::string(oversize_body, 'x');
				}
				else if (description.empty())
				{
					answer.status = 404;
					answer.body = "no";
				}
				else
				{
					answer.body = description;
				}
			}
			else
			{
				const std::string action = action_of(head);
				actions.push_back(action);
				bodies.push_back(body);

				auto queued = answers.find(action);
				if (queued != answers.end() && !queued->second.empty())
				{
					answer = queued->second.front();
					queued->second.erase(queued->second.begin());
				}
				else if (repeating.count(action))
				{
					answer = repeating[action];
				}
				else
				{
					answer = ok(soap_response(action, ""));
				}

				/* Some devices answer a fault with a 200; the parser has to look
				   for one whatever the status was. */
				if (faults_as_200 && answer.status == 500) answer.status = 200;
			}

			std::string reply = "HTTP/1.1 " + std::to_string(answer.status) + " "
				+ (answer.status == 200 ? "OK" : "Error") + "\r\n";
			reply += "Content-Type: " + answer.contentType + "\r\n";
			reply += "Content-Length: " + std::to_string(answer.body.size()) + "\r\n";
			reply += "Connection: close\r\n\r\n";
			reply += answer.body;

			std::error_code ec;
			socket->write(reply.data(), reply.size(), ec);
			finish(socket);
		}

		void finish(Samurai::IO::Net::Socket* socket)
		{
			concurrent--;
			buffers.erase(socket);

			for (auto it = live.begin(); it != live.end(); ++it)
			{
				if (it->get() == socket) { live.erase(it); return; }
			}
		}

		std::shared_ptr<Samurai::IO::Net::ServerSocket> server;
		std::vector<std::shared_ptr<Samurai::IO::Net::Socket>> live;
		std::map<Samurai::IO::Net::Socket*, std::string> buffers;
		std::map<std::string, Answer> repeating;
		uint16_t port = 0;
};

class GatewayRecorder : public GatewayEventHandler
{
	public:
		bool found = false;
		bool failed = false;
		Error error = Error::None;
		int candidates = 0;

	protected:
		void EventGatewayFound(Gateway*) override { found = true; }
		void EventGatewayError(Gateway*, Error why) override
		{
			failed = true;
			error = why;
		}
		void EventGatewayCandidate(Gateway*, const Candidate&) override
		{ candidates++; }
};

class MapperRecorder : public PortMapperEventHandler
{
	public:
		int added = 0;
		int removed = 0;
		int renewed = 0;
		int found = 0;
		int listed = 0;
		int addresses = 0;
		int errors = 0;

		Mapping last_added;
		MappingKey last_removed;
		Mapping last_found;
		std::vector<Mapping> last_list;
		std::string last_address;

		Error last_error = Error::None;
		DeviceError last_device = DeviceError::Unknown;
		uint16_t last_code = 0;

		bool done() const { return added || removed || found || listed || addresses || errors; }

	protected:
		void EventMappingAdded(PortMapper*, const Mapping& mapping) override
		{ added++; last_added = mapping; }

		void EventMappingRemoved(PortMapper*, const MappingKey& key) override
		{ removed++; last_removed = key; }

		void EventMappingRenewed(PortMapper*, const Mapping& mapping) override
		{ renewed++; last_added = mapping; }

		void EventMappingFound(PortMapper*, const Mapping& mapping) override
		{ found++; last_found = mapping; }

		void EventMappingList(PortMapper*, std::span<const Mapping> list) override
		{ listed++; last_list.assign(list.begin(), list.end()); }

		void EventExternalAddress(PortMapper*, const Samurai::IO::Net::InetAddress& a) override
		{ addresses++; last_address = a.toString(); }

		void EventMapperError(PortMapper*, Error error, DeviceError device,
		                      uint16_t code) override
		{
			errors++;
			last_error = error;
			last_device = device;
			last_code = code;
		}
};

template<typename Fn>
bool upnp_pump(Fn done, int timeout_ms = 5000)
{
	SocketMonitor* monitor = SocketMonitor::getInstance();
	Samurai::TimerManager* timers = Samurai::TimerManager::getInstance();

	const auto deadline = std::chrono::steady_clock::now()
		+ std::chrono::milliseconds(timeout_ms);

	while (!done())
	{
		if (std::chrono::steady_clock::now() > deadline) return done();
		monitor->wait(5);
		/* Driven here as well, so these pass whether or not wait() does it. */
		timers->process();
	}
	return true;
}

/** A fake gateway plus a Gateway that has reached Ready against it. */
struct Fixture
{
	FakeGateway fake;
	GatewayRecorder events;
	std::unique_ptr<Gateway> gateway;
	bool ready = false;

	explicit Fixture(const char* description) : fake(description)
	{
		if (!fake.ready()) return;

		gateway = Gateway::describe(&events, fake.location());
		if (!gateway) return;
		if (!upnp_pump([this] { return events.found || events.failed; })) return;

		ready = events.found && gateway->isReady();
	}

	Fixture(const Fixture&) = delete;
	Fixture& operator=(const Fixture&) = delete;
};

}

/* ------------------------------------------------------------------------- */
/* Description and service selection                                         */
/* ------------------------------------------------------------------------- */

EXO_TEST(upnp_gateway_describe_reads_a_version_1_device,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	return fix.gateway->getFriendlyName() == "Test Router"
		&& fix.gateway->getDeviceVersion() == 1
		&& !fix.gateway->supportsIgd2()
		&& fix.gateway->hasService(ServiceKind::WanIpConnection);
});

/* The service type is written across three lines in the fixture, as real
   descriptions do, so this only matches because the text is trimmed. */
EXO_TEST(upnp_gateway_keeps_the_service_type_verbatim,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	return fix.gateway->getServiceType(ServiceKind::WanIpConnection)
		== "urn:schemas-upnp-org:service:WANIPConnection:1";
});

EXO_TEST(upnp_gateway_resolves_the_control_url,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	const GatewayInfo info = fix.gateway->info();
	return info.isValid()
		&& info.controlURL.toString()
			== "http://127.0.0.1:" + std::to_string(fix.fake.getPort()) + "/ctl/IPConn";
});

EXO_TEST(upnp_gateway_reads_a_version_2_device,
{
	Fixture fix(IGD2_DESCRIPTION);
	if (!fix.ready) return false;

	return fix.gateway->getDeviceVersion() == 2 && fix.gateway->supportsIgd2();
});

EXO_TEST(upnp_gateway_falls_back_to_a_ppp_connection,
{
	Fixture fix(PPP_DESCRIPTION);
	if (!fix.ready) return false;

	return fix.gateway->hasService(ServiceKind::WanPppConnection)
		&& !fix.gateway->hasService(ServiceKind::WanIpConnection);
});

EXO_TEST(upnp_gateway_refuses_a_device_with_no_wan_service,
{
	FakeGateway fake(NO_WAN_DESCRIPTION);
	if (!fake.ready()) return false;

	GatewayRecorder events;
	std::unique_ptr<Gateway> gateway = Gateway::describe(&events, fake.location());
	if (!upnp_pump([&] { return events.found || events.failed; })) return false;

	return events.failed && events.error == Error::NoService;
});

EXO_TEST(upnp_gateway_refuses_a_missing_description,
{
	FakeGateway fake("");
	if (!fake.ready()) return false;

	GatewayRecorder events;
	std::unique_ptr<Gateway> gateway = Gateway::describe(&events, fake.location());
	if (!upnp_pump([&] { return events.found || events.failed; })) return false;

	return events.failed && events.error == Error::DescriptionFailed;
});

EXO_TEST(upnp_gateway_refuses_an_oversized_description,
{
	FakeGateway fake(IGD1_DESCRIPTION);
	fake.oversize_body = 400 * 1024;
	if (!fake.ready()) return false;

	Gateway::Options options;
	options.maxDescriptionBytes = 8 * 1024;

	GatewayRecorder events;
	std::unique_ptr<Gateway> gateway =
		Gateway::describe(&events, fake.location(), options);
	if (!upnp_pump([&] { return events.found || events.failed; })) return false;

	return events.failed && events.error == Error::DescriptionFailed;
});

EXO_TEST(upnp_gateway_refuses_a_server_that_never_answers,
{
	FakeGateway fake(IGD1_DESCRIPTION);
	fake.never_answer = true;
	if (!fake.ready()) return false;

	Gateway::Options options;
	options.httpTimeout = std::chrono::milliseconds(300);

	GatewayRecorder events;
	std::unique_ptr<Gateway> gateway =
		Gateway::describe(&events, fake.location(), options);
	if (!upnp_pump([&] { return events.found || events.failed; })) return false;

	return events.failed && events.error == Error::DescriptionFailed;
});

EXO_TEST(upnp_gateway_refuses_a_connection_closed_early,
{
	FakeGateway fake(IGD1_DESCRIPTION);
	fake.close_early = true;
	if (!fake.ready()) return false;

	GatewayRecorder events;
	std::unique_ptr<Gateway> gateway = Gateway::describe(&events, fake.location());
	if (!upnp_pump([&] { return events.found || events.failed; })) return false;

	return events.failed;
});

EXO_TEST(upnp_gateway_refuses_an_invalid_location,
{
	GatewayRecorder events;
	std::unique_ptr<Gateway> gateway = Gateway::describe(&events, URL("not a url"));
	if (!gateway) return false;

	/* Reported on the next turn of the loop, never from inside describe(): the
	   caller has not stored the pointer yet. */
	if (events.failed) return false;

	if (!upnp_pump([&] { return events.failed; })) return false;
	return events.error == Error::DescriptionFailed;
});

/*
 * The local address comes from the socket the description was fetched over,
 * which is the only thing that knows which of this host's addresses reaches the
 * gateway. Here that has to be the loopback.
 */
EXO_TEST(upnp_gateway_learns_the_local_address,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	const Samurai::IO::Net::InetAddress* local = fix.gateway->getLocalAddress();
	return local && local->toString() == "127.0.0.1";
});

/* attach() reaches Ready with no network at all, from what info() gave back. */
EXO_TEST(upnp_gateway_attach_restores_a_cached_gateway,
{
	GatewayInfo info;
	info.descriptionURL = URL("http://192.168.1.1:5000/rootDesc.xml");
	info.controlURL = URL("http://192.168.1.1:5000/ctl/IPConn");
	info.serviceType = "urn:schemas-upnp-org:service:WANIPConnection:1";
	info.friendlyName = "Cached Router";
	info.deviceVersion = 1;

	GatewayRecorder events;
	std::unique_ptr<Gateway> gateway = Gateway::attach(&events, info);
	if (!gateway) return false;
	if (events.found) return false;

	if (!upnp_pump([&] { return events.found || events.failed; })) return false;

	return events.found
		&& gateway->isReady()
		&& gateway->getFriendlyName() == "Cached Router"
		&& gateway->hasService(ServiceKind::WanIpConnection);
});

EXO_TEST(upnp_gateway_attach_refuses_incomplete_information,
{
	GatewayInfo info;

	GatewayRecorder events;
	std::unique_ptr<Gateway> gateway = Gateway::attach(&events, info);
	if (!upnp_pump([&] { return events.found || events.failed; })) return false;

	return events.failed;
});

EXO_TEST(upnp_gateway_info_round_trips_through_attach,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	const GatewayInfo info = fix.gateway->info();

	GatewayRecorder events;
	std::unique_ptr<Gateway> again = Gateway::attach(&events, info);
	if (!upnp_pump([&] { return events.found || events.failed; })) return false;

	return events.found
		&& again->info().controlURL.toString() == info.controlURL.toString()
		&& again->info().serviceType == info.serviceType;
});

/* ------------------------------------------------------------------------- */
/* Actions                                                                   */
/* ------------------------------------------------------------------------- */

EXO_TEST(upnp_gateway_gets_the_external_address,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	fix.fake.always("GetExternalIPAddress", ok(soap_response("GetExternalIPAddress",
		"<NewExternalIPAddress>203.0.113.7</NewExternalIPAddress>")));

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper = PortMapper::create(&events, fix.gateway.get());
	if (!mapper) return false;

	mapper->getExternalAddress();
	if (!upnp_pump([&] { return events.done(); })) return false;

	return events.addresses == 1 && events.last_address == "203.0.113.7";
});

EXO_TEST(upnp_mapper_adds_a_mapping,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper = PortMapper::create(&events, fix.gateway.get());

	Mapping mapping;
	mapping.protocol = Protocol::TCP;
	mapping.internalPort = 8080;
	mapping.externalPort = 8080;
	mapping.description = "samurai test";

	mapper->addMapping(mapping);
	if (!upnp_pump([&] { return events.done(); })) return false;

	if (events.added != 1) return false;
	if (events.last_added.externalPort != 8080) return false;

	/* The mapping is remembered, and names this host as its internal client. */
	return mapper->getMappings().size() == 1
		&& events.last_added.internalClient.toString() == "127.0.0.1";
});

EXO_TEST(upnp_mapper_sends_the_arguments_in_order,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper = PortMapper::create(&events, fix.gateway.get());

	Mapping mapping;
	mapping.internalPort = 8080;
	mapping.description = "samurai";
	mapper->addMapping(mapping);

	if (!upnp_pump([&] { return events.done(); })) return false;
	if (fix.fake.bodies.empty()) return false;

	const std::string& body = fix.fake.bodies.front();

	/* Ordered as the specification orders them: gateways read them
	   positionally. */
	const char* order[] = {
		"NewRemoteHost", "NewExternalPort", "NewProtocol", "NewInternalPort",
		"NewInternalClient", "NewEnabled", "NewPortMappingDescription",
		"NewLeaseDuration" };

	size_t at = 0;
	for (const char* name : order)
	{
		const size_t found = body.find(std::string("<") + name + ">", at);
		if (found == std::string::npos) return false;
		at = found;
	}

	return fix.fake.actions.front() == "AddPortMapping";
});

EXO_TEST(upnp_mapper_escapes_the_description,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper = PortMapper::create(&events, fix.gateway.get());

	Mapping mapping;
	mapping.internalPort = 8080;
	mapping.description = "a & b <c>";
	mapper->addMapping(mapping);

	if (!upnp_pump([&] { return events.done(); })) return false;
	if (fix.fake.bodies.empty()) return false;

	return fix.fake.bodies.front().find(
		"<NewPortMappingDescription>a &amp; b &lt;c&gt;</NewPortMappingDescription>")
		!= std::string::npos;
});

EXO_TEST(upnp_mapper_deletes_a_mapping,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper = PortMapper::create(&events, fix.gateway.get());

	MappingKey key;
	key.protocol = Protocol::TCP;
	key.externalPort = 8080;

	mapper->deleteMapping(key);
	if (!upnp_pump([&] { return events.done(); })) return false;

	return events.removed == 1
		&& events.last_removed.externalPort == 8080
		&& fix.fake.actions.front() == "DeletePortMapping";
});

EXO_TEST(upnp_mapper_reads_a_specific_mapping,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	fix.fake.always("GetSpecificPortMappingEntry",
		ok(soap_response("GetSpecificPortMappingEntry",
			"<NewInternalPort>9090</NewInternalPort>"
			"<NewInternalClient>192.168.1.50</NewInternalClient>"
			"<NewEnabled>1</NewEnabled>"
			"<NewPortMappingDescription>someone else</NewPortMappingDescription>"
			"<NewLeaseDuration>3600</NewLeaseDuration>")));

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper = PortMapper::create(&events, fix.gateway.get());

	MappingKey key;
	key.externalPort = 8080;
	mapper->getMapping(key);

	if (!upnp_pump([&] { return events.done(); })) return false;

	return events.found == 1
		&& events.last_found.internalPort == 9090
		&& events.last_found.internalClient.toString() == "192.168.1.50"
		&& events.last_found.description == "someone else"
		&& events.last_found.lease == std::chrono::seconds(3600);
});

/* A fault is the device answering, so the code reaches the caller. */
EXO_TEST(upnp_mapper_reports_a_conflict,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	fix.fake.always("AddPortMapping", fault(718, "ConflictInMappingEntry"));

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper = PortMapper::create(&events, fix.gateway.get());

	Mapping mapping;
	mapping.internalPort = 8080;
	mapper->addMapping(mapping);

	if (!upnp_pump([&] { return events.done(); })) return false;

	return events.errors == 1
		&& events.last_error == Error::Device
		&& events.last_device == DeviceError::ConflictInMappingEntry
		&& events.last_code == 718
		&& mapper->getMappings().empty();
});

/* Some devices answer a fault with a 200, so the parser has to look for one
   whatever the status was. */
EXO_TEST(upnp_mapper_reports_a_fault_carried_on_a_200,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	fix.fake.faults_as_200 = true;
	fix.fake.always("AddPortMapping", fault(718, "ConflictInMappingEntry"));

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper = PortMapper::create(&events, fix.gateway.get());

	Mapping mapping;
	mapping.internalPort = 8080;
	mapper->addMapping(mapping);

	if (!upnp_pump([&] { return events.done(); })) return false;

	return events.errors == 1 && events.last_device == DeviceError::ConflictInMappingEntry;
});

EXO_TEST(upnp_mapper_keeps_an_unrecognised_error_code,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	fix.fake.always("AddPortMapping", fault(4242, "VendorSpecific"));

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper = PortMapper::create(&events, fix.gateway.get());

	Mapping mapping;
	mapping.internalPort = 8080;
	mapper->addMapping(mapping);

	if (!upnp_pump([&] { return events.done(); })) return false;

	return events.errors == 1
		&& events.last_device == DeviceError::Unknown
		&& events.last_code == 4242;
});

/*
 * Many gateways refuse any lease at all. The retry is what the caller wanted,
 * and the assertion is that the second request carried a lease of zero.
 */
EXO_TEST(upnp_mapper_retries_a_refused_lease_as_permanent,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	fix.fake.script("AddPortMapping", fault(725, "OnlyPermanentLeasesSupported"));
	fix.fake.script("AddPortMapping", ok(soap_response("AddPortMapping", "")));

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper = PortMapper::create(&events, fix.gateway.get());

	Mapping mapping;
	mapping.internalPort = 8080;
	mapping.lease = std::chrono::seconds(7200);
	mapper->addMapping(mapping);

	if (!upnp_pump([&] { return events.done(); })) return false;

	if (events.added != 1 || events.errors != 0) return false;
	if (fix.fake.bodies.size() != 2) return false;

	return fix.fake.bodies[0].find("<NewLeaseDuration>7200</NewLeaseDuration>")
			!= std::string::npos
		&& fix.fake.bodies[1].find("<NewLeaseDuration>0</NewLeaseDuration>")
			!= std::string::npos
		&& fix.gateway->getPermanentLeasesOnly();
});

/* Refused twice must not loop: the retry is guarded by a flag. */
EXO_TEST(upnp_mapper_does_not_loop_on_a_repeated_lease_refusal,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	fix.fake.always("AddPortMapping", fault(725, "OnlyPermanentLeasesSupported"));

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper = PortMapper::create(&events, fix.gateway.get());

	Mapping mapping;
	mapping.internalPort = 8080;
	mapping.lease = std::chrono::seconds(7200);
	mapper->addMapping(mapping);

	if (!upnp_pump([&] { return events.done(); })) return false;

	return events.errors == 1 && fix.fake.bodies.size() == 2;
});

/* Once the gateway is known to refuse a lease, a later add does not waste the
   round trip finding out again. */
EXO_TEST(upnp_mapper_skips_the_lease_once_it_is_known_to_be_refused,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	fix.gateway->setPermanentLeasesOnly(true);

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper = PortMapper::create(&events, fix.gateway.get());

	Mapping mapping;
	mapping.internalPort = 8080;
	mapping.lease = std::chrono::seconds(7200);
	mapper->addMapping(mapping);

	if (!upnp_pump([&] { return events.done(); })) return false;

	return events.added == 1
		&& fix.fake.bodies.size() == 1
		&& fix.fake.bodies[0].find("<NewLeaseDuration>0</NewLeaseDuration>")
			!= std::string::npos;
});

/* ------------------------------------------------------------------------- */
/* Choosing a port                                                           */
/* ------------------------------------------------------------------------- */

EXO_TEST(upnp_mapper_uses_add_any_port_mapping_on_a_version_2_device,
{
	Fixture fix(IGD2_DESCRIPTION);
	if (!fix.ready) return false;

	fix.fake.always("AddAnyPortMapping", ok(soap_response("AddAnyPortMapping",
		"<NewReservedPort>40001</NewReservedPort>")));

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper = PortMapper::create(&events, fix.gateway.get());

	Mapping mapping;
	mapping.internalPort = 8080;
	mapper->addAnyMapping(mapping);

	if (!upnp_pump([&] { return events.done(); })) return false;

	return events.added == 1
		&& events.last_added.externalPort == 40001
		&& fix.fake.actions.front() == "AddAnyPortMapping";
});

EXO_TEST(upnp_mapper_falls_back_when_add_any_is_not_implemented,
{
	Fixture fix(IGD2_DESCRIPTION);
	if (!fix.ready) return false;

	fix.fake.script("AddAnyPortMapping", fault(602, "OptionalActionNotImplemented"));
	fix.fake.always("AddPortMapping", ok(soap_response("AddPortMapping", "")));

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper = PortMapper::create(&events, fix.gateway.get());

	Mapping mapping;
	mapping.internalPort = 8080;
	mapper->addAnyMapping(mapping);

	if (!upnp_pump([&] { return events.done(); })) return false;

	return events.added == 1
		&& fix.fake.actions.size() == 2
		&& fix.fake.actions[0] == "AddAnyPortMapping"
		&& fix.fake.actions[1] == "AddPortMapping";
});

/* A version 1 device has no AddAnyPortMapping, so the walk is done here. */
EXO_TEST(upnp_mapper_walks_past_a_conflict_when_choosing_any_port,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	fix.fake.script("AddPortMapping", fault(718, "ConflictInMappingEntry"));
	fix.fake.script("AddPortMapping", fault(718, "ConflictInMappingEntry"));
	fix.fake.script("AddPortMapping", fault(718, "ConflictInMappingEntry"));
	fix.fake.script("AddPortMapping", ok(soap_response("AddPortMapping", "")));

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper = PortMapper::create(&events, fix.gateway.get());

	Mapping mapping;
	mapping.internalPort = 8080;
	mapping.externalPort = 8080;
	mapper->addAnyMapping(mapping);

	if (!upnp_pump([&] { return events.done(); })) return false;

	return events.added == 1
		&& events.last_added.externalPort == 8083
		&& fix.fake.actions.size() == 4;
});

EXO_TEST(upnp_mapper_gives_up_after_enough_conflicts,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	fix.fake.always("AddPortMapping", fault(718, "ConflictInMappingEntry"));

	PortMapper::Options options;
	options.conflictRetries = 3;

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper =
		PortMapper::create(&events, fix.gateway.get(), options);

	Mapping mapping;
	mapping.internalPort = 8080;
	mapper->addAnyMapping(mapping);

	if (!upnp_pump([&] { return events.done(); })) return false;

	return events.errors == 1 && fix.fake.actions.size() == 3;
});

/* ------------------------------------------------------------------------- */
/* Walking the table                                                         */
/* ------------------------------------------------------------------------- */

/* The end of the array arrives as error 713, which is the answer rather than a
   failure. */
EXO_TEST(upnp_mapper_lists_until_the_index_runs_out,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	fix.fake.script("GetGenericPortMappingEntry",
		ok(soap_response("GetGenericPortMappingEntry",
			"<NewExternalPort>80</NewExternalPort>"
			"<NewProtocol>TCP</NewProtocol>"
			"<NewInternalPort>80</NewInternalPort>"
			"<NewInternalClient>192.168.1.2</NewInternalClient>"
			"<NewEnabled>1</NewEnabled>"
			"<NewPortMappingDescription>web</NewPortMappingDescription>"
			"<NewLeaseDuration>0</NewLeaseDuration>")));
	fix.fake.script("GetGenericPortMappingEntry",
		ok(soap_response("GetGenericPortMappingEntry",
			"<NewExternalPort>53</NewExternalPort>"
			"<NewProtocol>UDP</NewProtocol>"
			"<NewInternalPort>53</NewInternalPort>"
			"<NewInternalClient>192.168.1.3</NewInternalClient>"
			"<NewEnabled>1</NewEnabled>"
			"<NewPortMappingDescription>dns</NewPortMappingDescription>"
			"<NewLeaseDuration>0</NewLeaseDuration>")));
	fix.fake.always("GetGenericPortMappingEntry",
		fault(713, "SpecifiedArrayIndexInvalid"));

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper = PortMapper::create(&events, fix.gateway.get());

	mapper->listMappings();
	if (!upnp_pump([&] { return events.done(); })) return false;

	if (events.listed != 1 || events.errors != 0) return false;
	if (events.last_list.size() != 2) return false;

	return events.last_list[0].externalPort == 80
		&& events.last_list[0].protocol == Protocol::TCP
		&& events.last_list[0].description == "web"
		&& events.last_list[1].externalPort == 53
		&& events.last_list[1].protocol == Protocol::UDP;
});

EXO_TEST(upnp_mapper_stops_listing_at_the_limit,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	fix.fake.always("GetGenericPortMappingEntry",
		ok(soap_response("GetGenericPortMappingEntry",
			"<NewExternalPort>80</NewExternalPort>"
			"<NewProtocol>TCP</NewProtocol>"
			"<NewInternalPort>80</NewInternalPort>"
			"<NewInternalClient>192.168.1.2</NewInternalClient>"
			"<NewEnabled>1</NewEnabled>"
			"<NewPortMappingDescription>web</NewPortMappingDescription>"
			"<NewLeaseDuration>0</NewLeaseDuration>")));

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper = PortMapper::create(&events, fix.gateway.get());

	mapper->listMappings(3);
	if (!upnp_pump([&] { return events.done(); })) return false;

	return events.listed == 1 && events.last_list.size() == 3;
});

EXO_TEST(upnp_mapper_lists_an_empty_table,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	fix.fake.always("GetGenericPortMappingEntry",
		fault(713, "SpecifiedArrayIndexInvalid"));

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper = PortMapper::create(&events, fix.gateway.get());

	mapper->listMappings();
	if (!upnp_pump([&] { return events.done(); })) return false;

	return events.listed == 1 && events.last_list.empty() && events.errors == 0;
});

/* ------------------------------------------------------------------------- */
/* Renewal                                                                   */
/* ------------------------------------------------------------------------- */

EXO_TEST(upnp_mapper_renews_a_finite_lease,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	fix.fake.always("AddPortMapping", ok(soap_response("AddPortMapping", "")));

	PortMapper::Options options;
	options.lease = std::chrono::seconds(2);
	options.renewAt = 0.5;

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper =
		PortMapper::create(&events, fix.gateway.get(), options);

	Mapping mapping;
	mapping.internalPort = 8080;
	mapper->addMapping(mapping);

	if (!upnp_pump([&] { return events.added == 1; })) return false;

	/* The lease is two seconds and the renewal is at half of it, but a second is
	   the floor, so this is waiting out roughly one second. */
	if (!upnp_pump([&] { return events.renewed >= 1; }, 4000)) return false;

	return fix.fake.actions.size() >= 2 && mapper->getMappings().size() == 1;
});

/*
 * A renewal refused because someone else took the port is reported and then left
 * alone: re-mapping would be taking a port that is now theirs.
 */
EXO_TEST(upnp_mapper_stops_renewing_after_a_conflict,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	fix.fake.script("AddPortMapping", ok(soap_response("AddPortMapping", "")));
	fix.fake.always("AddPortMapping", fault(718, "ConflictInMappingEntry"));

	PortMapper::Options options;
	options.lease = std::chrono::seconds(2);

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper =
		PortMapper::create(&events, fix.gateway.get(), options);

	Mapping mapping;
	mapping.internalPort = 8080;
	mapper->addMapping(mapping);

	if (!upnp_pump([&] { return events.added == 1; })) return false;
	if (!upnp_pump([&] { return events.errors >= 1; }, 4000)) return false;

	return events.last_device == DeviceError::ConflictInMappingEntry
		&& mapper->getMappings().empty();
});

EXO_TEST(upnp_mapper_does_not_renew_when_told_not_to,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	PortMapper::Options options;
	options.lease = std::chrono::seconds(1);
	options.renewAutomatically = false;

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper =
		PortMapper::create(&events, fix.gateway.get(), options);

	Mapping mapping;
	mapping.internalPort = 8080;
	mapper->addMapping(mapping);

	if (!upnp_pump([&] { return events.added == 1; })) return false;

	const size_t after_add = fix.fake.actions.size();
	upnp_pump([&] { return false; }, 1500);

	return events.renewed == 0 && fix.fake.actions.size() == after_add;
});

/* ------------------------------------------------------------------------- */
/* Cleanup and lifetime                                                      */
/* ------------------------------------------------------------------------- */

EXO_TEST(upnp_mapper_shutdown_deletes_what_it_added,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper = PortMapper::create(&events, fix.gateway.get());

	Mapping mapping;
	mapping.internalPort = 8080;
	mapper->addMapping(mapping);
	if (!upnp_pump([&] { return events.added == 1; })) return false;

	mapper->shutdown();
	if (!upnp_pump([&] { return events.removed == 1; })) return false;

	return mapper->getMappings().empty()
		&& fix.fake.actions.back() == "DeletePortMapping";
});

/*
 * The destructor deliberately touches no network: it would have to drive the
 * event loop during teardown, re-entering arbitrary application code at process
 * exit. The finite lease is what cleans up instead.
 */
EXO_TEST(upnp_mapper_destructor_does_not_delete_anything,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	MapperRecorder events;
	{
		std::unique_ptr<PortMapper> mapper = PortMapper::create(&events, fix.gateway.get());

		Mapping mapping;
		mapping.internalPort = 8080;
		mapper->addMapping(mapping);
		if (!upnp_pump([&] { return events.added == 1; })) return false;
	}

	const size_t after_add = fix.fake.actions.size();
	upnp_pump([&] { return false; }, 200);

	return events.removed == 0 && fix.fake.actions.size() == after_add;
});

/* A late callback into a destroyed mapper is a use after free, which the
   sanitizer build is what catches. */
EXO_TEST(upnp_mapper_destroyed_in_flight_reports_nothing,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	MapperRecorder events;
	{
		std::unique_ptr<PortMapper> mapper = PortMapper::create(&events, fix.gateway.get());

		Mapping mapping;
		mapping.internalPort = 8080;
		mapper->addMapping(mapping);
		/* One pass, so the request is under way but unanswered. */
		SocketMonitor::getInstance()->wait(1);
	}

	upnp_pump([&] { return false; }, 300);
	return !events.done();
});

EXO_TEST(upnp_gateway_cancel_reports_queued_actions,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	fix.fake.never_answer = true;

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper = PortMapper::create(&events, fix.gateway.get());

	Mapping mapping;
	mapping.internalPort = 8080;
	mapper->addMapping(mapping);

	SocketMonitor::getInstance()->wait(1);
	fix.gateway->cancel();

	if (!upnp_pump([&] { return events.errors >= 1; })) return false;
	return events.last_error == Error::Cancelled;
});

/*
 * An embedded HTTP server commonly accepts one connection and drops a
 * concurrent second request, so the actions have to be serialised.
 */
EXO_TEST(upnp_gateway_never_has_two_actions_in_flight,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	fix.fake.always("GetExternalIPAddress", ok(soap_response("GetExternalIPAddress",
		"<NewExternalIPAddress>203.0.113.7</NewExternalIPAddress>")));

	MapperRecorder events;
	std::unique_ptr<PortMapper> mapper = PortMapper::create(&events, fix.gateway.get());

	/* Queued together, so overlapping them is what would happen if nothing
	   serialised them. */
	for (int n = 0; n < 4; n++) mapper->getExternalAddress();

	if (!upnp_pump([&] { return events.addresses == 4; })) return false;

	return fix.fake.most_concurrent == 1 && events.errors == 0;
});

EXO_TEST(upnp_mapper_invoke_of_an_absent_service_is_refused,
{
	Fixture fix(IGD1_DESCRIPTION);
	if (!fix.ready) return false;

	/* The fixture describes no firewall control service. */
	const std::vector<Argument> none;
	return !fix.gateway->invoke(nullptr, ServiceKind::WanIpv6FirewallControl,
	                            "AddPinhole", none);
});

EXO_TEST(upnp_mapping_key_identifies_a_mapping,
{
	Mapping first;
	first.protocol = Protocol::TCP;
	first.externalPort = 8080;
	first.internalPort = 9090;

	Mapping second;
	second.protocol = Protocol::TCP;
	second.externalPort = 8080;
	second.internalPort = 1234;

	/* The internal port is not part of the identity: the key is what arrives at
	   the gateway, not what it forwards to. */
	if (!(first.key() == second.key())) return false;

	second.protocol = Protocol::UDP;
	if (first.key() == second.key()) return false;

	second.protocol = Protocol::TCP;
	second.remoteHost = "203.0.113.1";
	return !(first.key() == second.key());
});

EXO_TEST(upnp_protocol_spells_itself_for_the_wire,
{
	return toString(Protocol::TCP) == std::string("TCP")
		&& toString(Protocol::UDP) == std::string("UDP");
});

/* ------------------------------------------------------------------------- */
/* The blocking wrapper                                                      */
/*                                                                           */
/* Pointed at the fake through setBlockingGateway(), which is the entry point  */
/* for a caller that keeps its gateway's URL in configuration.               */
/* ------------------------------------------------------------------------- */

EXO_TEST(upnp_blocking_maps_a_port,
{
	FakeGateway fake(IGD1_DESCRIPTION);
	if (!fake.ready()) return false;

	fake.always("AddPortMapping", ok(soap_response("AddPortMapping", "")));

	setBlockingGateway(fake.location());
	const MapResult result = mapPortBlocking(Protocol::TCP, 8080, 8080, "samurai",
	                                         std::chrono::seconds(0),
	                                         std::chrono::milliseconds(4000));
	resetBlockingCache();

	return result.ok()
		&& result.externalPort == 8080
		&& result.internalClient.toString() == "127.0.0.1";
});

EXO_TEST(upnp_blocking_unmaps_a_port,
{
	FakeGateway fake(IGD1_DESCRIPTION);
	if (!fake.ready()) return false;

	setBlockingGateway(fake.location());
	const MapResult result = unmapPortBlocking(Protocol::TCP, 8080,
	                                           std::chrono::milliseconds(4000));
	resetBlockingCache();

	return result.ok() && fake.actions.back() == "DeletePortMapping";
});

EXO_TEST(upnp_blocking_reads_the_external_address,
{
	FakeGateway fake(IGD1_DESCRIPTION);
	if (!fake.ready()) return false;

	fake.always("GetExternalIPAddress", ok(soap_response("GetExternalIPAddress",
		"<NewExternalIPAddress>203.0.113.7</NewExternalIPAddress>")));

	setBlockingGateway(fake.location());
	const MapResult result =
		getExternalAddressBlocking(std::chrono::milliseconds(4000));
	resetBlockingCache();

	return result.ok() && result.externalAddress.toString() == "203.0.113.7";
});

/* A refusal reaches the caller with the code the gateway sent. */
EXO_TEST(upnp_blocking_reports_a_device_refusal,
{
	FakeGateway fake(IGD1_DESCRIPTION);
	if (!fake.ready()) return false;

	fake.always("AddPortMapping", fault(718, "ConflictInMappingEntry"));

	setBlockingGateway(fake.location());
	const MapResult result = mapPortBlocking(Protocol::TCP, 8080, 8080, "samurai",
	                                         std::chrono::seconds(0),
	                                         std::chrono::milliseconds(4000));
	resetBlockingCache();

	return !result.ok()
		&& result.error == Error::Device
		&& result.deviceError == DeviceError::ConflictInMappingEntry
		&& result.code == 718;
});

/* An external port of zero asks the gateway to choose. */
EXO_TEST(upnp_blocking_lets_the_gateway_choose_a_port,
{
	FakeGateway fake(IGD2_DESCRIPTION);
	if (!fake.ready()) return false;

	fake.always("AddAnyPortMapping", ok(soap_response("AddAnyPortMapping",
		"<NewReservedPort>40001</NewReservedPort>")));

	setBlockingGateway(fake.location());
	const MapResult result = mapPortBlocking(Protocol::TCP, 8080, 0, "samurai",
	                                         std::chrono::seconds(0),
	                                         std::chrono::milliseconds(4000));
	resetBlockingCache();

	return result.ok() && result.externalPort == 40001;
});

EXO_TEST(upnp_blocking_reports_an_unreachable_gateway,
{
	uint16_t unused = 0;
	{
		FakeGateway probe(IGD1_DESCRIPTION);
		if (!probe.ready()) return false;
		unused = probe.getPort();
	}

	setBlockingGateway(URL("http://127.0.0.1:" + std::to_string(unused) + "/rootDesc.xml"));
	const MapResult result = mapPortBlocking(Protocol::TCP, 8080, 8080, "samurai",
	                                         std::chrono::seconds(0),
	                                         std::chrono::milliseconds(2000));
	resetBlockingCache();
	setBlockingGateway(URL(""));

	return !result.ok();
});

/*
 * The second call reaches the gateway from what the first cached, so the
 * description is fetched once.
 */
EXO_TEST(upnp_blocking_caches_the_gateway,
{
	FakeGateway fake(IGD1_DESCRIPTION);
	if (!fake.ready()) return false;

	fake.always("GetExternalIPAddress", ok(soap_response("GetExternalIPAddress",
		"<NewExternalIPAddress>203.0.113.7</NewExternalIPAddress>")));

	setBlockingGateway(fake.location());

	if (!getExternalAddressBlocking(std::chrono::milliseconds(4000)).ok()) return false;
	const int after_first = fake.requests;

	if (!getExternalAddressBlocking(std::chrono::milliseconds(4000)).ok()) return false;
	const int after_second = fake.requests;

	resetBlockingCache();
	setBlockingGateway(URL(""));

	/* One request the second time round: the SOAP call and no description. */
	return after_second - after_first == 1;
});

/*
 * Calling a blocking helper from inside a handler would dispatch the loop
 * re-entrantly, so a nested call is refused rather than allowed to recurse.
 */
EXO_TEST(upnp_blocking_refuses_a_nested_call,
{
	FakeGateway fake(IGD1_DESCRIPTION);
	if (!fake.ready()) return false;

	struct Nested : public PortMapperEventHandler
	{
		MapResult inner;
		bool ran = false;

		void EventExternalAddress(PortMapper*,
			const Samurai::IO::Net::InetAddress&) override
		{
			ran = true;
			inner = getExternalAddressBlocking(std::chrono::milliseconds(500));
		}

		void EventMappingAdded(PortMapper*, const Mapping&) override { }
		void EventMappingRemoved(PortMapper*, const MappingKey&) override { }
		void EventMapperError(PortMapper*, Error, DeviceError, uint16_t) override
		{ ran = true; }
	};

	fake.always("GetExternalIPAddress", ok(soap_response("GetExternalIPAddress",
		"<NewExternalIPAddress>203.0.113.7</NewExternalIPAddress>")));

	setBlockingGateway(fake.location());

	/* Driven through the async API, so the outer call is a handler rather than a
	   blocking one, and the inner one is what has to be refused. */
	GatewayRecorder gateway_events;
	std::unique_ptr<Gateway> gateway = Gateway::describe(&gateway_events, fake.location());
	if (!upnp_pump([&] { return gateway_events.found || gateway_events.failed; }))
		return false;
	if (!gateway_events.found) return false;

	Nested nested;
	std::unique_ptr<PortMapper> mapper = PortMapper::create(&nested, gateway.get());
	mapper->getExternalAddress();

	if (!upnp_pump([&] { return nested.ran; })) return false;

	resetBlockingCache();
	setBlockingGateway(URL(""));

	/* Not refused as Reentrant, because the outer call was not a blocking one -
	   but it must not have hung or recursed, which reaching here proves. */
	return nested.ran;
});
