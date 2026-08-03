/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include "description.h"

#include <samurai/io/xml.h>
#include <samurai/util/string.h>
#include <samurai/stdc.h>

namespace {

using Samurai::IO::XmlElement;

constexpr const char* IP_CONNECTION  = "urn:schemas-upnp-org:service:WANIPConnection:";
constexpr const char* PPP_CONNECTION = "urn:schemas-upnp-org:service:WANPPPConnection:";
constexpr const char* IPV6_FIREWALL  = "urn:schemas-upnp-org:service:WANIPv6FirewallControl:";
constexpr const char* GATEWAY_DEVICE = "urn:schemas-upnp-org:device:InternetGatewayDevice:";

/** The trailing version of a service or device type, or 0. */
int trailing_version(std::string_view type, std::string_view prefix)
{
	if (type.size() <= prefix.size()) return 0;

	const std::string_view digits = type.substr(prefix.size());
	if (digits.empty()) return 0;

	for (const char digit : digits)
		if (digit < '0' || digit > '9') return 0;

	const uint64_t value = Samurai::Util::Convert::to_uint64(std::string(digits));
	return (value && value < 100) ? (int) value : 0;
}

/** Collect every <service> in the tree, however deeply nested. */
void collect_services(const XmlElement* element,
                      std::vector<const XmlElement*>& out)
{
	for (const auto& child : element->getChildren())
	{
		if (child->getName() == "service") out.push_back(child.get());
		collect_services(child.get(), out);
	}
}

}


const Samurai::IO::Net::UPnP::Service*
Samurai::IO::Net::UPnP::DeviceDescription::find(ServiceKind kind) const
{
	const Service* best = nullptr;
	for (const Service& service : services)
	{
		if (service.kind != kind) continue;
		if (!best || service.version > best->version) best = &service;
	}
	return best;
}


const Samurai::IO::Net::UPnP::Service*
Samurai::IO::Net::UPnP::DeviceDescription::preferredConnection() const
{
	/* An IP connection is preferred over a PPP one, and within either the higher
	   version wins. A device offering both is offering the same mapping table
	   through two front ends. */
	if (const Service* ip = find(ServiceKind::WanIpConnection)) return ip;
	return find(ServiceKind::WanPppConnection);
}


bool Samurai::IO::Net::UPnP::Description::classify(std::string_view serviceType,
                                                   ServiceKind& kind, int& version)
{
	if (Samurai::Util::istarts_with(serviceType, IP_CONNECTION))
	{
		const int found = trailing_version(serviceType, IP_CONNECTION);
		if (!found) return false;
		kind = ServiceKind::WanIpConnection;
		version = found;
		return true;
	}

	if (Samurai::Util::istarts_with(serviceType, PPP_CONNECTION))
	{
		const int found = trailing_version(serviceType, PPP_CONNECTION);
		if (!found) return false;
		kind = ServiceKind::WanPppConnection;
		version = found;
		return true;
	}

	if (Samurai::Util::istarts_with(serviceType, IPV6_FIREWALL))
	{
		const int found = trailing_version(serviceType, IPV6_FIREWALL);
		if (!found) return false;
		kind = ServiceKind::WanIpv6FirewallControl;
		version = found;
		return true;
	}

	return false;
}


Samurai::IO::Net::UPnP::Error
Samurai::IO::Net::UPnP::Description::parse(std::string_view body, const URL& location,
                                           DeviceDescription& out)
{
	out = DeviceDescription();

	if (!location.isValid()) return Error::DescriptionFailed;

	Samurai::IO::XmlDocument document;
	if (document.parse(body) != Samurai::IO::XmlError::Ok) return Error::DescriptionFailed;

	const XmlElement* root = document.getRoot();
	if (!root || root->getName() != "root") return Error::DescriptionFailed;

	const XmlElement* device = root->findChild("device");
	if (!device) return Error::DescriptionFailed;

	out.friendlyName = device->getChildText("friendlyName");
	out.deviceType = device->getChildText("deviceType");
	out.udn = device->getChildText("UDN");

	if (Samurai::Util::istarts_with(out.deviceType, GATEWAY_DEVICE))
	{
		const int version = trailing_version(out.deviceType, GATEWAY_DEVICE);
		if (version) out.deviceVersion = version;
	}

	/*
	 * URLBase when the document gives one, the description's own location
	 * otherwise. A URLBase with a trailing slash against a controlURL with a
	 * leading one is a pair routers really send, and URL::resolve() is what
	 * keeps it from becoming a doubled slash.
	 */
	const std::string base_text = root->getChildText("URLBase");
	const URL base = base_text.empty() ? location : URL(base_text);
	if (!base.isValid()) return Error::DescriptionFailed;

	std::vector<const XmlElement*> found;
	collect_services(root, found);

	for (const XmlElement* element : found)
	{
		const std::string serviceType = element->getChildText("serviceType");
		const std::string controlPath = element->getChildText("controlURL");
		if (serviceType.empty() || controlPath.empty()) continue;

		ServiceKind kind = ServiceKind::WanIpConnection;
		int version = 1;
		if (!Description::classify(serviceType, kind, version)) continue;

		const URL control = base.resolve(controlPath);
		if (!control.isValid()) continue;

		/*
		 * A control URL that names another host would let a device on the local
		 * network aim requests wherever it liked, and no real gateway does it.
		 * A different port is normal - the description is often on one port and
		 * the control on another - so only the host and the scheme are checked.
		 */
		if (!Samurai::Util::iequals(control.getScheme(), "http")) continue;
		if (!Samurai::Util::iequals(control.getHostname(), location.getHostname())) continue;

		Service service;
		service.kind = kind;
		service.serviceType = serviceType;
		service.controlURL = control;
		service.version = version;
		out.services.push_back(service);
	}

	if (out.services.empty()) return Error::NoService;
	return Error::None;
}
