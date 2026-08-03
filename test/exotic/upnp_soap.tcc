/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/net/upnp/error.h>
#include <samurai/io/net/upnp/gateway.h>
#include <samurai/io/net/url.h>

#include "io/net/upnp/description.h"
#include "io/net/upnp/soap.h"
#include "io/net/upnp/ssdp.h"

#include <optional>
#include <set>
#include <string>
#include <vector>

/*
 * The three parsers, driven directly. All of it is string in and struct out, so
 * nothing here touches the network or needs a gateway.
 *
 * These headers are private to the implementation, which the suite reaches
 * through the src/ include path the autotest target already carries.
 */

namespace {

using Samurai::IO::Net::URL;
using namespace Samurai::IO::Net::UPnP;

/** Parse a response body, reporting what came back. */
Error read_response(const std::string& body, ActionResult& result,
                    std::optional<ActionFault>& fault)
{
	return Soap::parseResponse(body, result, fault);
}

std::string envelope(const std::string& inner)
{
	return std::string("<?xml version=\"1.0\"?>")
		+ "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">"
		+ "<s:Body>" + inner + "</s:Body></s:Envelope>";
}

std::string fault_body(int code)
{
	return envelope(
		std::string("<s:Fault><faultcode>s:Client</faultcode>")
		+ "<faultstring>UPnPError</faultstring><detail>"
		+ "<UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\">"
		+ "<errorCode>" + std::to_string(code) + "</errorCode>"
		+ "<errorDescription>because</errorDescription>"
		+ "</UPnPError></detail></s:Fault>");
}

const char* IGD_DESCRIPTION =
	"<?xml version=\"1.0\"?>\n"
	"<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\n"
	" <URLBase>http://192.168.1.1:5000/</URLBase>\n"
	" <device>\n"
	"  <deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>\n"
	"  <friendlyName>Test Router</friendlyName>\n"
	"  <UDN>uuid:abc</UDN>\n"
	"  <deviceList><device>\n"
	"   <deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType>\n"
	"   <deviceList><device>\n"
	"    <deviceType>urn:schemas-upnp-org:device:WANConnectionDevice:1</deviceType>\n"
	"    <serviceList><service>\n"
	"     <serviceType>\n"
	"       urn:schemas-upnp-org:service:WANIPConnection:1\n"
	"     </serviceType>\n"
	"     <controlURL>/ctl/IPConn</controlURL>\n"
	"    </service></serviceList>\n"
	"   </device></deviceList>\n"
	"  </device></deviceList>\n"
	" </device>\n"
	"</root>\n";

const URL LOCATION("http://192.168.1.1:5000/rootDesc.xml");

}

/* ------------------------------------------------------------------------- */
/* Building a request                                                        */
/* ------------------------------------------------------------------------- */

EXO_TEST(upnp_soap_action_header_is_quoted,
{
	return Soap::buildActionHeader("urn:schemas-upnp-org:service:WANIPConnection:1",
	                               "AddPortMapping")
		== "\"urn:schemas-upnp-org:service:WANIPConnection:1#AddPortMapping\"";
});

EXO_TEST(upnp_soap_request_names_the_action_and_the_service,
{
	const std::vector<Argument> none;
	const std::string body =
		Soap::buildRequest("urn:x:1", "GetExternalIPAddress", none);

	return body.find("<u:GetExternalIPAddress xmlns:u=\"urn:x:1\">") != std::string::npos
		&& body.find("</u:GetExternalIPAddress>") != std::string::npos
		&& body.find("<s:Body>") != std::string::npos
		&& body.find("</s:Envelope>") != std::string::npos;
});

/* Ordered as given: gateways read their arguments positionally. */
EXO_TEST(upnp_soap_request_keeps_the_argument_order,
{
	std::vector<Argument> args;
	args.emplace_back("First", "1");
	args.emplace_back("Second", "2");
	args.emplace_back("Third", "3");

	const std::string body = Soap::buildRequest("urn:x:1", "Act", args);

	const size_t first = body.find("<First>");
	const size_t second = body.find("<Second>");
	const size_t third = body.find("<Third>");

	return first != std::string::npos && second > first && third > second;
});

EXO_TEST(upnp_soap_request_escapes_its_values,
{
	std::vector<Argument> args;
	args.emplace_back("NewPortMappingDescription", "a & b <c> \"d\"");

	const std::string body = Soap::buildRequest("urn:x:1", "Act", args);

	return body.find("a &amp; b &lt;c&gt; &quot;d&quot;") != std::string::npos
		&& body.find("<c>") == std::string::npos;
});

EXO_TEST(upnp_soap_request_escapes_the_service_type,
{
	const std::vector<Argument> none;
	const std::string body = Soap::buildRequest("urn:x&y:1", "Act", none);
	return body.find("xmlns:u=\"urn:x&amp;y:1\"") != std::string::npos;
});

/* ------------------------------------------------------------------------- */
/* Reading a response                                                        */
/* ------------------------------------------------------------------------- */

EXO_TEST(upnp_soap_reads_an_empty_response,
{
	ActionResult result;
	std::optional<ActionFault> fault;

	const std::string body = envelope(
		"<u:AddPortMappingResponse xmlns:u=\"urn:x:1\"></u:AddPortMappingResponse>");

	return read_response(body, result, fault) == Error::None
		&& !fault.has_value()
		&& result.arguments.empty();
});

EXO_TEST(upnp_soap_reads_out_arguments,
{
	ActionResult result;
	std::optional<ActionFault> fault;

	const std::string body = envelope(
		"<u:GetExternalIPAddressResponse xmlns:u=\"urn:x:1\">"
		"<NewExternalIPAddress>203.0.113.7</NewExternalIPAddress>"
		"</u:GetExternalIPAddressResponse>");

	if (read_response(body, result, fault) != Error::None) return false;

	return result.value("NewExternalIPAddress") == "203.0.113.7"
		&& result.find("NewExternalIPAddress") != nullptr
		&& result.find("Nonesuch") == nullptr
		&& result.value("Nonesuch").empty();
});

EXO_TEST(upnp_soap_reads_every_out_argument,
{
	ActionResult result;
	std::optional<ActionFault> fault;

	const std::string body = envelope(
		"<u:GetSpecificPortMappingEntryResponse xmlns:u=\"urn:x:1\">"
		"<NewInternalPort>9090</NewInternalPort>"
		"<NewInternalClient>192.168.1.50</NewInternalClient>"
		"<NewEnabled>1</NewEnabled>"
		"<NewPortMappingDescription>web</NewPortMappingDescription>"
		"<NewLeaseDuration>3600</NewLeaseDuration>"
		"</u:GetSpecificPortMappingEntryResponse>");

	if (read_response(body, result, fault) != Error::None) return false;

	return result.arguments.size() == 5
		&& result.value("NewInternalPort") == "9090"
		&& result.value("NewLeaseDuration") == "3600";
});

/* The prefixes are the vendor's choice, so matching is on the local name. */
EXO_TEST(upnp_soap_reads_a_response_with_any_prefix,
{
	ActionResult result;
	std::optional<ActionFault> fault;

	const std::string body =
		"<?xml version=\"1.0\"?>"
		"<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\">"
		"<SOAP-ENV:Body><m:ActResponse xmlns:m=\"urn:x:1\">"
		"<Value>7</Value></m:ActResponse></SOAP-ENV:Body></SOAP-ENV:Envelope>";

	return read_response(body, result, fault) == Error::None
		&& result.value("Value") == "7";
});

EXO_TEST(upnp_soap_reads_a_response_with_no_prefix,
{
	ActionResult result;
	std::optional<ActionFault> fault;

	const std::string body =
		"<?xml version=\"1.0\"?><Envelope><Body><ActResponse>"
		"<Value>7</Value></ActResponse></Body></Envelope>";

	return read_response(body, result, fault) == Error::None
		&& result.value("Value") == "7";
});

/* Not required to be named after the action: devices misname it, and there is
   nothing else the single element under Body could be. */
EXO_TEST(upnp_soap_accepts_a_misnamed_response_element,
{
	ActionResult result;
	std::optional<ActionFault> fault;

	const std::string body = envelope(
		"<u:SomethingElse xmlns:u=\"urn:x:1\"><Value>7</Value></u:SomethingElse>");

	return read_response(body, result, fault) == Error::None
		&& result.value("Value") == "7";
});

EXO_TEST(upnp_soap_trims_an_out_argument,
{
	ActionResult result;
	std::optional<ActionFault> fault;

	const std::string body = envelope(
		"<u:ActResponse xmlns:u=\"urn:x:1\">"
		"<NewExternalIPAddress>\n  203.0.113.7 \t</NewExternalIPAddress>"
		"</u:ActResponse>");

	return read_response(body, result, fault) == Error::None
		&& result.value("NewExternalIPAddress") == "203.0.113.7";
});

/* ------------------------------------------------------------------------- */
/* Faults                                                                   */
/* ------------------------------------------------------------------------- */

EXO_TEST(upnp_soap_reads_a_fault,
{
	ActionResult result;
	std::optional<ActionFault> fault;

	if (read_response(fault_body(718), result, fault) != Error::Device) return false;
	if (!fault.has_value()) return false;

	return fault->code == 718
		&& fault->error == DeviceError::ConflictInMappingEntry
		&& fault->description == "because"
		&& result.arguments.empty();
});

/* The code is kept as sent, so a vendor's own survives not being recognised. */
EXO_TEST(upnp_soap_keeps_an_unrecognised_fault_code,
{
	ActionResult result;
	std::optional<ActionFault> fault;

	if (read_response(fault_body(4242), result, fault) != Error::Device) return false;
	return fault->code == 4242 && fault->error == DeviceError::Unknown;
});

EXO_TEST(upnp_soap_reads_a_fault_with_no_upnp_error,
{
	ActionResult result;
	std::optional<ActionFault> fault;

	const std::string body = envelope(
		"<s:Fault><faultcode>s:Server</faultcode>"
		"<faultstring>something went wrong</faultstring></s:Fault>");

	if (read_response(body, result, fault) != Error::Device) return false;

	/* Still a refusal, which is better than reporting a malformed body. */
	return fault.has_value()
		&& fault->code == 0
		&& fault->description == "something went wrong";
});

EXO_TEST(upnp_soap_refuses_a_body_that_is_not_an_envelope,
{
	ActionResult result;
	std::optional<ActionFault> fault;

	return read_response("<html>not soap</html>", result, fault) == Error::BadResponse
		&& read_response("", result, fault) == Error::BadResponse
		&& read_response("<s:Envelope/>", result, fault) == Error::BadResponse;
});

EXO_TEST(upnp_soap_refuses_malformed_xml,
{
	ActionResult result;
	std::optional<ActionFault> fault;
	return read_response("<s:Envelope><s:Body>", result, fault) == Error::BadResponse;
});

EXO_TEST(upnp_soap_refuses_a_body_with_two_elements,
{
	ActionResult result;
	std::optional<ActionFault> fault;

	const std::string body = envelope("<u:A xmlns:u=\"urn:x\"/><u:B xmlns:u=\"urn:x\"/>");
	return read_response(body, result, fault) == Error::BadResponse;
});

/* Every prefix, under the sanitizer, is what covers reading off the end. */
EXO_TEST(upnp_soap_every_prefix_of_a_response_is_answered,
{
	const std::string body = envelope(
		"<u:ActResponse xmlns:u=\"urn:x:1\"><Value>7</Value></u:ActResponse>");

	for (size_t n = 0; n <= body.size(); n++)
	{
		ActionResult result;
		std::optional<ActionFault> fault;
		const Error error = Soap::parseResponse(body.substr(0, n), result, fault);
		if (error != Error::None && error != Error::BadResponse && error != Error::Device)
			return false;
	}
	return true;
});

EXO_TEST(upnp_soap_every_prefix_of_a_fault_is_answered,
{
	const std::string body = fault_body(725);

	for (size_t n = 0; n <= body.size(); n++)
	{
		ActionResult result;
		std::optional<ActionFault> fault;
		const Error error = Soap::parseResponse(body.substr(0, n), result, fault);
		if (error != Error::None && error != Error::BadResponse && error != Error::Device)
			return false;
	}
	return true;
});

/* ------------------------------------------------------------------------- */
/* Error naming                                                             */
/* ------------------------------------------------------------------------- */

/*
 * Walked over the enumerators rather than a list written out here, which a value
 * added later would not reach, and compared against the fallback rather than
 * against empty, since that is what a value with no case of its own returns.
 *
 * Device is last; anything inserted before it is covered by construction.
 */
EXO_TEST(upnp_error_names_every_value,
{
	for (int n = 0; n <= (int) Error::Device; n++)
	{
		const char* name = toString((Error) n);
		if (!name || !*name) return false;
		if (std::string(name) == "unknown error") return false;
	}
	return true;
});

/*
 * DeviceError carries the protocol's own numbers, so there is no range to walk.
 * Driven from toDeviceError() instead: every code it recognises has to have a
 * name, which fails for an enumerator added to one and not the other.
 */
EXO_TEST(upnp_device_error_names_every_value,
{
	const std::string fallback = "unrecognised error code";
	size_t named = 0;

	for (uint32_t code = 0; code <= 0xffff; code++)
	{
		const DeviceError error = toDeviceError((uint16_t) code);
		if (error == DeviceError::Unknown) continue;

		const char* name = toString(error);
		if (!name || !*name) return false;
		if (std::string(name) == fallback) return false;
		named++;
	}

	/* Unknown is the one value the fallback belongs to. */
	if (std::string(toString(DeviceError::Unknown)) != fallback) return false;

	/* And the mapping is not empty, which would make the loop above vacuous. */
	return named >= 20;
});

EXO_TEST(upnp_device_error_maps_the_codes_it_knows,
{
	return toDeviceError(718) == DeviceError::ConflictInMappingEntry
		&& toDeviceError(725) == DeviceError::OnlyPermanentLeasesSupported
		&& toDeviceError(713) == DeviceError::SpecifiedArrayIndexInvalid
		&& toDeviceError(402) == DeviceError::InvalidArgs
		&& toDeviceError(0) == DeviceError::Unknown
		&& toDeviceError(9999) == DeviceError::Unknown;
});

/* ------------------------------------------------------------------------- */
/* Device description                                                        */
/* ------------------------------------------------------------------------- */

EXO_TEST(upnp_description_finds_a_nested_wan_service,
{
	DeviceDescription description;
	if (Description::parse(IGD_DESCRIPTION, LOCATION, description) != Error::None)
		return false;

	if (description.friendlyName != "Test Router") return false;
	if (description.deviceVersion != 1) return false;
	if (description.udn != "uuid:abc") return false;

	const Service* service = description.preferredConnection();
	return service
		&& service->kind == ServiceKind::WanIpConnection
		/* Whitespace inside the element is trimmed, which real descriptions
		   need. */
		&& service->serviceType == "urn:schemas-upnp-org:service:WANIPConnection:1"
		/* URLBase has a trailing slash and the control URL a leading one, and
		   the result must not have two. */
		&& service->controlURL.toString() == "http://192.168.1.1:5000/ctl/IPConn";
});

EXO_TEST(upnp_description_uses_the_location_when_there_is_no_url_base,
{
	const char* document =
		"<root><device>"
		"<deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>"
		"<serviceList><service>"
		"<serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>"
		"<controlURL>/ctl/IPConn</controlURL>"
		"</service></serviceList></device></root>";

	DeviceDescription description;
	if (Description::parse(document, LOCATION, description) != Error::None) return false;

	return description.preferredConnection()->controlURL.toString()
		== "http://192.168.1.1:5000/ctl/IPConn";
});

EXO_TEST(upnp_description_resolves_a_relative_control_url,
{
	const char* document =
		"<root><device>"
		"<deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>"
		"<serviceList><service>"
		"<serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>"
		"<controlURL>ctl/IPConn</controlURL>"
		"</service></serviceList></device></root>";

	DeviceDescription description;
	const URL location("http://192.168.1.1:5000/upnp/rootDesc.xml");
	if (Description::parse(document, location, description) != Error::None) return false;

	return description.preferredConnection()->controlURL.toString()
		== "http://192.168.1.1:5000/upnp/ctl/IPConn";
});

/* The description is often on one port and the control on another. */
EXO_TEST(upnp_description_allows_a_control_url_on_another_port,
{
	const char* document =
		"<root><device>"
		"<deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>"
		"<serviceList><service>"
		"<serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>"
		"<controlURL>http://192.168.1.1:49152/ctl/IPConn</controlURL>"
		"</service></serviceList></device></root>";

	DeviceDescription description;
	if (Description::parse(document, LOCATION, description) != Error::None) return false;

	return description.preferredConnection()->controlURL.getEffectivePort() == 49152;
});

/*
 * A control URL naming another host would let a device on the local network aim
 * requests wherever it liked, and no real gateway does it. Refused rather than
 * made optional.
 */
EXO_TEST(upnp_description_refuses_an_off_host_control_url,
{
	const char* document =
		"<root><device>"
		"<deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>"
		"<serviceList><service>"
		"<serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>"
		"<controlURL>http://evil.example.org/ctl</controlURL>"
		"</service></serviceList></device></root>";

	DeviceDescription description;
	return Description::parse(document, LOCATION, description) == Error::NoService;
});

EXO_TEST(upnp_description_refuses_a_non_http_control_url,
{
	const char* document =
		"<root><device>"
		"<deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>"
		"<serviceList><service>"
		"<serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>"
		"<controlURL>ftp://192.168.1.1/ctl</controlURL>"
		"</service></serviceList></device></root>";

	DeviceDescription description;
	return Description::parse(document, LOCATION, description) == Error::NoService;
});

EXO_TEST(upnp_description_prefers_the_higher_version,
{
	const char* document =
		"<root><device>"
		"<deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:2</deviceType>"
		"<serviceList>"
		"<service>"
		"<serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>"
		"<controlURL>/ctl/one</controlURL>"
		"</service>"
		"<service>"
		"<serviceType>urn:schemas-upnp-org:service:WANIPConnection:2</serviceType>"
		"<controlURL>/ctl/two</controlURL>"
		"</service>"
		"</serviceList></device></root>";

	DeviceDescription description;
	if (Description::parse(document, LOCATION, description) != Error::None) return false;

	const Service* service = description.preferredConnection();
	return description.deviceVersion == 2
		&& service->version == 2
		&& service->controlURL.getPath() == "/ctl/two";
});

/* An IP connection is preferred over a PPP one; both are the same table behind
   two front ends. */
EXO_TEST(upnp_description_prefers_ip_over_ppp,
{
	const char* document =
		"<root><device>"
		"<deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>"
		"<serviceList>"
		"<service>"
		"<serviceType>urn:schemas-upnp-org:service:WANPPPConnection:1</serviceType>"
		"<controlURL>/ctl/ppp</controlURL>"
		"</service>"
		"<service>"
		"<serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>"
		"<controlURL>/ctl/ip</controlURL>"
		"</service>"
		"</serviceList></device></root>";

	DeviceDescription description;
	if (Description::parse(document, LOCATION, description) != Error::None) return false;

	return description.preferredConnection()->kind == ServiceKind::WanIpConnection;
});

EXO_TEST(upnp_description_finds_a_ppp_connection_alone,
{
	const char* document =
		"<root><device>"
		"<deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>"
		"<serviceList><service>"
		"<serviceType>urn:schemas-upnp-org:service:WANPPPConnection:1</serviceType>"
		"<controlURL>/ctl/ppp</controlURL>"
		"</service></serviceList></device></root>";

	DeviceDescription description;
	if (Description::parse(document, LOCATION, description) != Error::None) return false;

	return description.preferredConnection()->kind == ServiceKind::WanPppConnection;
});

EXO_TEST(upnp_description_refuses_a_device_with_no_connection_service,
{
	const char* document =
		"<root><device>"
		"<deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>"
		"<serviceList><service>"
		"<serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType>"
		"<controlURL>/ctl/content</controlURL>"
		"</service></serviceList></device></root>";

	DeviceDescription description;
	return Description::parse(document, LOCATION, description) == Error::NoService;
});

EXO_TEST(upnp_description_refuses_a_document_that_is_not_a_description,
{
	DeviceDescription description;
	return Description::parse("<html/>", LOCATION, description) == Error::DescriptionFailed
		&& Description::parse("", LOCATION, description) == Error::DescriptionFailed
		&& Description::parse("<root/>", LOCATION, description) == Error::DescriptionFailed;
});

EXO_TEST(upnp_description_survives_a_missing_xml_declaration_and_a_bom,
{
	const std::string document = std::string("\xef\xbb\xbf") +
		"<root><device>"
		"<deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>"
		"<serviceList><service>"
		"<serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>"
		"<controlURL>/ctl/IPConn</controlURL>"
		"</service></serviceList></device></root>";

	DeviceDescription description;
	return Description::parse(document, LOCATION, description) == Error::None;
});

EXO_TEST(upnp_description_finds_the_firewall_service_when_present,
{
	const char* document =
		"<root><device>"
		"<deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:2</deviceType>"
		"<serviceList>"
		"<service>"
		"<serviceType>urn:schemas-upnp-org:service:WANIPConnection:2</serviceType>"
		"<controlURL>/ctl/ip</controlURL>"
		"</service>"
		"<service>"
		"<serviceType>urn:schemas-upnp-org:service:WANIPv6FirewallControl:1</serviceType>"
		"<controlURL>/ctl/fw</controlURL>"
		"</service>"
		"</serviceList></device></root>";

	DeviceDescription description;
	if (Description::parse(document, LOCATION, description) != Error::None) return false;

	/* The firewall service is a different thing from port mapping, so it must
	   not be what preferredConnection() hands back. */
	return description.find(ServiceKind::WanIpv6FirewallControl) != nullptr
		&& description.preferredConnection()->kind == ServiceKind::WanIpConnection;
});

EXO_TEST(upnp_description_classify_reads_the_version,
{
	ServiceKind kind = ServiceKind::WanPppConnection;
	int version = 0;

	if (!Description::classify("urn:schemas-upnp-org:service:WANIPConnection:2",
	                           kind, version)) return false;
	if (kind != ServiceKind::WanIpConnection || version != 2) return false;

	if (!Description::classify("urn:schemas-upnp-org:service:WANPPPConnection:1",
	                           kind, version)) return false;
	if (kind != ServiceKind::WanPppConnection || version != 1) return false;

	return !Description::classify("urn:schemas-upnp-org:service:ContentDirectory:1",
	                              kind, version)
		&& !Description::classify("", kind, version)
		&& !Description::classify("urn:schemas-upnp-org:service:WANIPConnection:",
		                          kind, version)
		&& !Description::classify("urn:schemas-upnp-org:service:WANIPConnection:x",
		                          kind, version);
});

EXO_TEST(upnp_description_every_prefix_is_answered,
{
	const std::string document(IGD_DESCRIPTION);

	for (size_t n = 0; n <= document.size(); n++)
	{
		DeviceDescription description;
		const Error error = Description::parse(document.substr(0, n), LOCATION, description);
		if (error != Error::None && error != Error::DescriptionFailed &&
		    error != Error::NoService) return false;
	}
	return true;
});

/* ------------------------------------------------------------------------- */
/* SSDP request and reply                                                    */
/* ------------------------------------------------------------------------- */

EXO_TEST(upnp_ssdp_search_has_the_required_headers,
{
	const std::string search = Ssdp::buildSearch("239.255.255.250:1900",
		"urn:schemas-upnp-org:device:InternetGatewayDevice:1", 2);

	if (search.find("M-SEARCH * HTTP/1.1\r\n") != 0) return false;

	/* The quotes around ssdp:discover are part of the value, and a gateway that
	   checks will refuse the search without them. */
	return search.find("HOST: 239.255.255.250:1900\r\n") != std::string::npos
		&& search.find("MAN: \"ssdp:discover\"\r\n") != std::string::npos
		&& search.find("ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n")
			!= std::string::npos
		&& search.find("MX: 2\r\n") != std::string::npos
		&& search.find("USER-AGENT: ") != std::string::npos
		/* Terminated by a blank line, so the head is complete. */
		&& search.size() > 4
		&& search.compare(search.size() - 4, 4, "\r\n\r\n") == 0;
});

EXO_TEST(upnp_ssdp_search_starts_with_the_request_line,
{
	const std::string search = Ssdp::buildSearch("host", "st", 3);
	return search.find("M-SEARCH * HTTP/1.1\r\n") == 0
		&& search.find('\n') == 20;
});

/* Not ssdp:all: that draws a reply from every device on the network, each of
   which would then have to be fetched to find out it is not a gateway. */
EXO_TEST(upnp_ssdp_search_targets_are_gateway_specific,
{
	const std::span<const char* const> targets = Ssdp::searchTargets();
	if (targets.empty()) return false;

	for (const char* target : targets)
	{
		const std::string_view text(target);
		if (text == "ssdp:all") return false;
		if (text.find("InternetGatewayDevice") == std::string_view::npos &&
		    text.find("WANIPConnection") == std::string_view::npos &&
		    text.find("WANPPPConnection") == std::string_view::npos) return false;
	}

	/* The device types are asked first, being the right question. */
	return std::string_view(targets[0]).find("InternetGatewayDevice:2")
		!= std::string_view::npos;
});

/* A gateway is one hop away, so the organisation and global scoped groups would
   cost datagrams and find nothing. */
EXO_TEST(upnp_ssdp_groups_are_link_and_site_scoped_only,
{
	const std::span<const Ssdp::Group> all = Ssdp::groups();
	if (all.size() != 3) return false;

	bool ipv4 = false, link_local = false, site_local = false;
	for (const Ssdp::Group& group : all)
	{
		if (group.port != 1900) return false;
		const std::string_view address(group.address);
		if (address == "239.255.255.250") ipv4 = true;
		if (address == "ff02::c") link_local = true;
		if (address == "ff05::c") site_local = true;
		if (address == "ff08::c" || address == "ff0e::c") return false;
	}

	return ipv4 && link_local && site_local;
});

namespace {

const Samurai::IO::Net::InetAddress SOURCE("192.168.1.1");

std::string reply_text(const std::string& extra = "",
                       const std::string& location = "http://192.168.1.1:5000/desc.xml")
{
	std::string out = "HTTP/1.1 200 OK\r\n";
	out += "CACHE-CONTROL: max-age=1800\r\n";
	out += "EXT:\r\n";
	if (!location.empty()) out += "LOCATION: " + location + "\r\n";
	out += "SERVER: Linux/3.4 UPnP/1.1 Router/1.0\r\n";
	out += "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n";
	out += "USN: uuid:abc::urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n";
	out += extra;
	out += "\r\n";
	return out;
}

}

EXO_TEST(upnp_ssdp_reads_a_reply,
{
	Ssdp::Reply reply;
	if (!Ssdp::parseReply(reply_text(), SOURCE, true, reply)) return false;

	return reply.location.toString() == "http://192.168.1.1:5000/desc.xml"
		&& reply.st == "urn:schemas-upnp-org:device:InternetGatewayDevice:1"
		&& reply.usn == "uuid:abc::urn:schemas-upnp-org:device:InternetGatewayDevice:1"
		&& reply.server == "Linux/3.4 UPnP/1.1 Router/1.0"
		&& reply.maxAge == 1800
		&& reply.source.toString() == "192.168.1.1";
});

EXO_TEST(upnp_ssdp_reply_header_names_are_case_insensitive,
{
	std::string text = "HTTP/1.1 200 OK\r\n";
	text += "location: http://192.168.1.1:5000/desc.xml\r\n";
	text += "usn: uuid:abc\r\n";
	text += "\r\n";

	Ssdp::Reply reply;
	return Ssdp::parseReply(text, SOURCE, true, reply) && reply.usn == "uuid:abc";
});

/* Real devices emit bare LF, and refusing them means not finding them. */
EXO_TEST(upnp_ssdp_reply_accepts_bare_lf,
{
	std::string text = "HTTP/1.1 200 OK\n";
	text += "LOCATION: http://192.168.1.1:5000/desc.xml\n";
	text += "USN: uuid:abc\n";
	text += "\n";

	Ssdp::Reply reply;
	return Ssdp::parseReply(text, SOURCE, true, reply);
});

EXO_TEST(upnp_ssdp_reply_without_a_location_is_refused,
{
	Ssdp::Reply reply;
	return !Ssdp::parseReply(reply_text("", ""), SOURCE, true, reply);
});

EXO_TEST(upnp_ssdp_reply_without_a_usn_is_refused,
{
	std::string text = "HTTP/1.1 200 OK\r\n";
	text += "LOCATION: http://192.168.1.1:5000/desc.xml\r\n";
	text += "\r\n";

	Ssdp::Reply reply;
	return !Ssdp::parseReply(text, SOURCE, true, reply);
});

/* An announcement is not an answer to a search. */
EXO_TEST(upnp_ssdp_notify_is_refused,
{
	std::string text = "NOTIFY * HTTP/1.1\r\n";
	text += "LOCATION: http://192.168.1.1:5000/desc.xml\r\n";
	text += "USN: uuid:abc\r\n";
	text += "NTS: ssdp:alive\r\n";
	text += "\r\n";

	Ssdp::Reply reply;
	return !Ssdp::parseReply(text, SOURCE, true, reply);
});

EXO_TEST(upnp_ssdp_non_success_status_is_refused,
{
	std::string text = "HTTP/1.1 404 Not Found\r\n";
	text += "LOCATION: http://192.168.1.1:5000/desc.xml\r\n";
	text += "USN: uuid:abc\r\n\r\n";

	Ssdp::Reply reply;
	return !Ssdp::parseReply(text, SOURCE, true, reply);
});

EXO_TEST(upnp_ssdp_truncated_reply_is_refused,
{
	const std::string text = reply_text();

	Ssdp::Reply reply;
	return !Ssdp::parseReply(text.substr(0, text.size() / 2), SOURCE, true, reply);
});

EXO_TEST(upnp_ssdp_reply_with_a_bad_location_is_refused,
{
	Ssdp::Reply reply;
	return !Ssdp::parseReply(reply_text("", "not a url"), SOURCE, true, reply)
		&& !Ssdp::parseReply(reply_text("", "ftp://192.168.1.1/x"), SOURCE, true, reply);
});

/*
 * Without this, any host on the network can answer with a location naming a
 * third party, and everything downstream is anchored to an address the device
 * chose.
 */
EXO_TEST(upnp_ssdp_reply_from_the_wrong_source_is_refused,
{
	const Samurai::IO::Net::InetAddress elsewhere("192.168.1.99");

	Ssdp::Reply reply;
	if (Ssdp::parseReply(reply_text(), elsewhere, true, reply)) return false;

	/* Accepted with the check turned off, which is what the option is for. */
	return Ssdp::parseReply(reply_text(), elsewhere, false, reply);
});

/*
 * The key is the location, not the USN.
 *
 * A device answers once per search target it matches, and its USN carries the
 * target with it, so keying on the USN would make one gateway look like four.
 * The location is what would be fetched, which is what the deduplication is
 * there to avoid doing twice.
 */
EXO_TEST(upnp_ssdp_reply_key_is_the_location,
{
	Ssdp::Reply reply;
	if (!Ssdp::parseReply(reply_text(), SOURCE, true, reply)) return false;
	return reply.key() == "http://192.168.1.1:5000/desc.xml"
		&& reply.key() != reply.usn;
});

/*
 * The shape a real gateway sends, taken from a UniFi Dream Machine running
 * MiniUPnPd 2.3.8: four replies to the four search targets, under USNs that
 * differ - and under two different UUIDs, one for the device announcements and
 * another for the service ones, so not even the UUID inside the USN collapses
 * them. All four name one description, and so are one candidate.
 */
EXO_TEST(upnp_ssdp_one_device_answering_every_target_is_one_candidate,
{
	const char* usns[] = {
		"uuid:d24ea61f-52a3-46f5-81cf-b4012f1ca284::"
			"urn:schemas-upnp-org:device:InternetGatewayDevice:2",
		"uuid:d24ea61f-52a3-46f5-81cf-b4012f1ca284::"
			"urn:schemas-upnp-org:device:InternetGatewayDevice:1",
		"uuid:d24ea61f-52a3-46f5-81cf-b4012f1ca286::"
			"urn:schemas-upnp-org:service:WANIPConnection:1",
		"uuid:d24ea61f-52a3-46f5-81cf-b4012f1ca286::"
			"urn:schemas-upnp-org:service:WANPPPConnection:1" };

	std::set<std::string> keys;
	for (const char* usn : usns)
	{
		std::string text = "HTTP/1.1 200 OK\r\n";
		text += "LOCATION: http://192.168.1.1:45141/rootDesc.xml\r\n";
		text += std::string("USN: ") + usn + "\r\n";
		text += "SERVER: Linux/5.4 UPnP/1.1 MiniUPnPd/2.3.8\r\n\r\n";

		Ssdp::Reply reply;
		if (!Ssdp::parseReply(text, SOURCE, false, reply)) return false;
		keys.insert(reply.key());
	}

	return keys.size() == 1;
});

/* Two genuinely different gateways stay two candidates. */
EXO_TEST(upnp_ssdp_two_devices_are_two_candidates,
{
	Ssdp::Reply first;
	Ssdp::Reply second;

	if (!Ssdp::parseReply(reply_text("", "http://192.168.1.1:5000/desc.xml"),
	                      SOURCE, false, first)) return false;
	if (!Ssdp::parseReply(reply_text("", "http://192.168.1.2:5000/desc.xml"),
	                      SOURCE, false, second)) return false;

	return first.key() != second.key();
});

/* A device sending an empty USN is still identified, because the location does
   the identifying. */
EXO_TEST(upnp_ssdp_reply_with_an_empty_usn_is_still_keyed,
{
	std::string text = "HTTP/1.1 200 OK\r\n";
	text += "LOCATION: http://192.168.1.1:5000/desc.xml\r\n";
	text += "USN: \r\n\r\n";

	Ssdp::Reply reply;
	if (!Ssdp::parseReply(text, SOURCE, true, reply)) return false;
	return reply.key() == "http://192.168.1.1:5000/desc.xml";
});

EXO_TEST(upnp_ssdp_reply_without_a_cache_control_has_no_max_age,
{
	std::string text = "HTTP/1.1 200 OK\r\n";
	text += "LOCATION: http://192.168.1.1:5000/desc.xml\r\n";
	text += "USN: uuid:abc\r\n\r\n";

	Ssdp::Reply reply;
	return Ssdp::parseReply(text, SOURCE, true, reply) && reply.maxAge == 0;
});

EXO_TEST(upnp_ssdp_every_prefix_of_a_reply_is_answered,
{
	const std::string text = reply_text();

	for (size_t n = 0; n <= text.size(); n++)
	{
		Ssdp::Reply reply;
		/* Only that it answers rather than reading off the end. */
		Ssdp::parseReply(text.substr(0, n), SOURCE, true, reply);
	}
	return true;
});
