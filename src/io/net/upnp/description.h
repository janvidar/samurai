/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_UPNP_DESCRIPTION_H
#define HAVE_SAMURAI_UPNP_DESCRIPTION_H

#include <samurai/io/net/upnp/error.h>
#include <samurai/io/net/upnp/gateway.h>
#include <samurai/io/net/url.h>

#include <string>
#include <string_view>
#include <vector>

namespace Samurai {
namespace IO {
namespace Net {
namespace UPnP {

/** One service from a device description. */
struct Service
{
	ServiceKind kind = ServiceKind::WanIpConnection;
	/** As advertised, byte for byte: the SOAPAction header repeats it. */
	std::string serviceType;
	/** Absolute, resolved against URLBase or the description's location. */
	URL controlURL{""};
	int version = 1;
};

/** What a device description says that is worth keeping. */
struct DeviceDescription
{
	std::string friendlyName;
	std::string deviceType;
	std::string udn;
	int deviceVersion = 1;
	std::vector<Service> services;

	const Service* find(ServiceKind kind) const;

	/**
	 * The service to talk to about port mapping: an IP connection if there is
	 * one, and a PPP connection otherwise. The higher version wins.
	 */
	const Service* preferredConnection() const;
};

namespace Description
{

/**
 * Parse a root device description.
 *
 * The device tree is walked recursively: a WAN connection device sits under a
 * WAN device under the root, and vendors vary the depth, so a walk that only
 * looked a fixed distance down would miss it on real hardware.
 *
 * Every service's control URL is resolved against <URLBase> when the document
 * has one and against 'location' otherwise, and then checked: the host has to
 * match 'location' and the scheme has to be http. A control URL pointing
 * somewhere else would let a device on the local network aim authenticated-
 * looking requests at a third party, and no real gateway does it.
 *
 * @return DescriptionFailed when the document cannot be read, NoService when it
 *         is readable but describes no connection service.
 */
Error parse(std::string_view body, const URL& location, DeviceDescription& out);

/** Which kind a service type names, if it is one of interest. */
bool classify(std::string_view serviceType, ServiceKind& kind, int& version);

}

}
}
}
}

#endif // HAVE_SAMURAI_UPNP_DESCRIPTION_H
