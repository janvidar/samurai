/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include "soap.h"

#include <samurai/io/xml.h>
#include <samurai/util/string.h>
#include <samurai/stdc.h>

std::string Samurai::IO::Net::UPnP::Soap::buildActionHeader(std::string_view serviceType,
                                                            std::string_view action)
{
	std::string out;
	out += '"';
	out.append(serviceType);
	out += '#';
	out.append(action);
	out += '"';
	return out;
}


std::string Samurai::IO::Net::UPnP::Soap::buildRequest(std::string_view serviceType,
                                                       std::string_view action,
                                                       std::span<const Argument> arguments)
{
	/*
	 * Written out rather than built through a writer: the envelope is fixed and
	 * the only part that varies is the values, so the whole of the escaping is
	 * one call per value - which is what a writer would have amounted to.
	 */
	std::string out;
	out += "<?xml version=\"1.0\"?>\r\n";
	out += "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\""
	       " s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n";
	out += "<s:Body>\r\n";

	out += "<u:";
	out.append(action);
	out += " xmlns:u=\"";
	out += Samurai::Util::xml_escape(serviceType);
	out += "\">\r\n";

	for (const Argument& argument : arguments)
	{
		out += '<';
		out += argument.first;
		out += '>';
		out += Samurai::Util::xml_escape(argument.second);
		out += "</";
		out += argument.first;
		out += ">\r\n";
	}

	out += "</u:";
	out.append(action);
	out += ">\r\n";
	out += "</s:Body>\r\n";
	out += "</s:Envelope>\r\n";

	return out;
}


Samurai::IO::Net::UPnP::Error
Samurai::IO::Net::UPnP::Soap::parseResponse(std::string_view body,
                                            ActionResult& result,
                                            std::optional<ActionFault>& fault)
{
	result.arguments.clear();
	fault.reset();

	Samurai::IO::XmlDocument document;
	if (document.parse(body) != Samurai::IO::XmlError::Ok) return Error::BadResponse;

	const Samurai::IO::XmlElement* root = document.getRoot();
	if (!root || root->getName() != "Envelope") return Error::BadResponse;

	const Samurai::IO::XmlElement* soap_body = root->findChild("Body");
	if (!soap_body) return Error::BadResponse;

	/*
	 * The fault is looked for first and regardless of the HTTP status, because
	 * some gateways answer 200 with a <UPnPError> inside. Finding it as a
	 * descendant rather than at a fixed depth: it sits under <detail>, whose
	 * nesting the specification leaves loose enough that vendors differ.
	 */
	const Samurai::IO::XmlElement* soap_fault = soap_body->findChild("Fault");
	if (soap_fault)
	{
		ActionFault reported;

		const Samurai::IO::XmlElement* upnp = soap_fault->findDescendant("UPnPError");
		if (upnp)
		{
			const std::string code = upnp->getChildText("errorCode");
			if (!code.empty())
			{
				const uint64_t value = Samurai::Util::Convert::to_uint64(code);
				if (value <= 0xffff) reported.code = (uint16_t) value;
			}
			reported.error = toDeviceError(reported.code);
			reported.description = upnp->getChildText("errorDescription");
		}

		/* A fault with no readable UPnPError is still a refusal, and saying so
		   is better than reporting a malformed body. */
		if (reported.description.empty())
			reported.description = soap_fault->getChildText("faultstring");

		fault = reported;
		return Error::Device;
	}

	/*
	 * The single element under <Body> is the response. Not required to be named
	 * '<action>Response': devices misname it, and there is nothing else it
	 * could be.
	 */
	const Samurai::IO::XmlElement* response = nullptr;
	for (const auto& child : soap_body->getChildren())
	{
		if (response) return Error::BadResponse;
		response = child.get();
	}

	if (!response) return Error::BadResponse;

	for (const auto& argument : response->getChildren())
	{
		result.arguments.emplace_back(
			argument->getName(),
			std::string(Samurai::Util::trim(argument->getText())));
	}

	return Error::None;
}
