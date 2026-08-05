/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/net/interface.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/net/hardwareaddress.h>
#include <memory>
#include <vector>
#include <string>
#include <string.h>

/*
 * Before this file the only thing that touched interface.cpp - 540 lines, the
 * largest source file in the library - was the 'interfaces' example program,
 * which is not run by the suite.
 *
 * These assert only what must hold on any host: a machine running the suite has
 * a loopback interface, and every entry the enumeration returns has to be
 * internally consistent. Nothing here depends on a particular adapter name,
 * address or count.
 */

using Interfaces = std::vector<std::unique_ptr<Samurai::IO::Net::NetworkInterface>>;

static bool enumerate(Interfaces& out)
{
	return Samurai::IO::Net::NetworkInterface::getInterfaces(out) && !out.empty();
}

EXO_TEST(interface_enumeration_succeeds,
{
	Interfaces list;
	return enumerate(list);
});

/* getInterfaces() appends, so a second call must not discard the first. */
EXO_TEST(interface_enumeration_appends,
{
	Interfaces list;
	if (!enumerate(list)) return false;
	const size_t first = list.size();

	if (!Samurai::IO::Net::NetworkInterface::getInterfaces(list)) return false;
	return list.size() == first * 2;
});

EXO_TEST(interface_enumeration_yields_no_null_entries,
{
	Interfaces list;
	if (!enumerate(list)) return false;

	for (const auto& iface : list)
		if (!iface) return false;
	return true;
});

/* Every host running this suite has a loopback interface. */
EXO_TEST(interface_enumeration_finds_loopback,
{
	Interfaces list;
	if (!enumerate(list)) return false;

	for (const auto& iface : list)
		if (iface->isLoopback()) return true;
	return false;
});

EXO_TEST(interface_loopback_address_is_loopback,
{
	Interfaces list;
	if (!enumerate(list)) return false;

	for (const auto& iface : list)
	{
		if (!iface->isLoopback()) continue;

		Samurai::IO::Net::InetAddress* addr = iface->getAddress();
		/* A loopback entry without an address tells us nothing; skip it and
		   let another one answer. */
		if (!addr) continue;
		if (addr->isLoopback()) return true;
	}
	return false;
});

EXO_TEST(interface_names_are_present_and_terminated,
{
	Interfaces list;
	if (!enumerate(list)) return false;

	for (const auto& iface : list)
	{
		const char* name = iface->getName();
		if (!name) return false;

		/* Bounded so an unterminated buffer fails here rather than running on. */
		if (strnlen(name, 256) == 0 || strnlen(name, 256) == 256) return false;
	}
	return true;
});

EXO_TEST(interface_mtu_is_sane_where_reported,
{
	Interfaces list;
	if (!enumerate(list)) return false;

	for (const auto& iface : list)
	{
		const int mtu = iface->getMtu();
		/* 0 means the platform did not report one. Anything else has to be at
		   least the IPv4 minimum and no more than a jumbo frame. */
		if (mtu == 0) continue;
		if (mtu < 68 || mtu > 65536) return false;
	}
	return true;
});

/* A loopback interface is not a point-to-point link, whatever else it is. */
EXO_TEST(interface_flags_are_self_consistent,
{
	Interfaces list;
	if (!enumerate(list)) return false;

	for (const auto& iface : list)
		if (iface->isLoopback() && iface->isPointToPoint()) return false;
	return true;
});

/* Reading a predicate twice must give the same answer - it reports stored
   state rather than re-querying the platform each time. */
EXO_TEST(interface_predicates_are_stable,
{
	Interfaces list;
	if (!enumerate(list)) return false;

	for (const auto& iface : list)
	{
		if (iface->isLoopback() != iface->isLoopback()) return false;
		if (iface->isEnabled() != iface->isEnabled()) return false;
		if (iface->isMulticast() != iface->isMulticast()) return false;
		if (iface->isBroadcast() != iface->isBroadcast()) return false;
	}
	return true;
});

/* An address handed out stays valid for the interface's lifetime, so the same
   pointer must come back rather than a fresh allocation per call. */
EXO_TEST(interface_address_is_stable_across_calls,
{
	Interfaces list;
	if (!enumerate(list)) return false;

	for (const auto& iface : list)
		if (iface->getAddress() != iface->getAddress()) return false;
	return true;
});

EXO_TEST(interface_hardware_address_is_formatted_when_present,
{
	Interfaces list;
	if (!enumerate(list)) return false;

	for (const auto& iface : list)
	{
		Samurai::IO::Net::HardwareAddress* hw = iface->getHWAddress();
		if (!hw) continue;

		const char* text = hw->getAddress();
		if (!text || strnlen(text, 32) != 17) return false;
	}
	return true;
});

/* Looking an interface up by the name the enumeration reported must find one. */
EXO_TEST(interface_lookup_by_name,
{
	Interfaces list;
	if (!enumerate(list)) return false;

	for (const auto& iface : list)
	{
		if (!iface->isLoopback()) continue;

		std::unique_ptr<Samurai::IO::Net::NetworkInterface> found =
			Samurai::IO::Net::NetworkInterface::getInterface(iface->getName());

		if (!found) return false;
		return strcmp(found->getName(), iface->getName()) == 0;
	}
	return true;
});

EXO_TEST(interface_lookup_by_unknown_name_returns_null,
{
	return !Samurai::IO::Net::NetworkInterface::getInterface("no-such-interface-0");
});

/* The entries own their contents: destroying the vector must not leave the
   sanitizer or 'leaks' with anything, and must not double free. */
EXO_TEST(interface_entries_release_cleanly,
{
	for (int pass = 0; pass < 3; pass++)
	{
		Interfaces list;
		if (!enumerate(list)) return false;
		list.clear();
	}
	return true;
});

/* Looking up by address must find the interface that reported it. */
EXO_TEST(interface_lookup_by_address,
{
	Interfaces list;
	if (!enumerate(list)) return false;

	for (const auto& iface : list)
	{
		Samurai::IO::Net::InetAddress* addr = iface->getAddress();
		if (!addr) continue;

		std::unique_ptr<Samurai::IO::Net::NetworkInterface> found =
			Samurai::IO::Net::NetworkInterface::getInterface(*addr);

		if (!found) return false;
		Samurai::IO::Net::InetAddress* got = found->getAddress();
		return got && *got == *addr;
	}
	return true;
});

EXO_TEST(interface_lookup_by_null_name_returns_null,
{
	return !Samurai::IO::Net::NetworkInterface::getInterface((const char*) nullptr);
});
