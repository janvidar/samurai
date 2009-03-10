#include <samurai/stdc.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/net/socketmonitor.h>

/* BASIC TESTS */

EXO_TEST(inet_init,
{
	return Samurai::IO::Net::SocketMonitor::getInstance() != 0;
});


EXO_TEST(inet_addr_invalid_1,
{
	Samurai::IO::Net::InetAddress addr;
	return !addr.isValid();
});

EXO_TEST(inet_addr_invalid_2,
{
	Samurai::IO::Net::InetAddress addr;
	return addr.getType() == Samurai::IO::Net::InetAddress::Unspecified;
});

EXO_TEST(inet_addr_invalid_3,
{
	Samurai::IO::Net::InetAddress addr;
	return !addr.isMulticast();
});

EXO_TEST(inet_addr_invalid_4,
{
	Samurai::IO::Net::InetAddress addr;
	return !addr.isLoopback();
});

EXO_TEST(inet_addr_invalid_5,
{
	Samurai::IO::Net::InetAddress addr;
	return !addr.isPrivate();
});


EXO_TEST(inet_addr_ipv4_basic_1,
{
	Samurai::IO::Net::InetAddress addr("1.2.3.4", Samurai::IO::Net::InetAddress::IPv4);
	return addr.isValid();
});

EXO_TEST(inet_addr_ipv4_basic_2,
{
	Samurai::IO::Net::InetAddress addr("001.002.003.004", Samurai::IO::Net::InetAddress::IPv4);
	return addr.isValid();
});

EXO_TEST(inet_addr_ipv4_basic_3,
{
	Samurai::IO::Net::InetAddress addr("1.2.3.4", Samurai::IO::Net::InetAddress::IPv4);
	return addr.isResolved();
});

EXO_TEST(inet_addr_ipv4_basic_4,
{
	Samurai::IO::Net::InetAddress addr("localhost");
	return !addr.isResolved();
});

EXO_TEST(inet_addr_ipv4_basic_5,
{
	Samurai::IO::Net::InetAddress addr("localhost");
	return addr.getType() == Samurai::IO::Net::InetAddress::Unspecified;
});

EXO_TEST(inet_addr_ipv4_basic_6,
{
	Samurai::IO::Net::InetAddress addr("localhost");
	return !addr.isValid();
});

EXO_TEST(inet_addr_ipv4_basic_7,
{
	Samurai::IO::Net::InetAddress addr("1.2.3.4");
	return addr.getType() == Samurai::IO::Net::InetAddress::IPv4;
});

EXO_TEST(inet_addr_ipv4_basic_8,
{
	Samurai::IO::Net::InetAddress addr("1.2.3.4");
	return addr.getType() != Samurai::IO::Net::InetAddress::IPv6;
});

EXO_TEST(inet_addr_ipv4_string_compare_1,
{
	Samurai::IO::Net::InetAddress addr("1.2.3.4");
	return strcmp(addr.toString(), "1.2.3.4") == 0;
});

EXO_TEST(inet_addr_ipv4_string_compare_2,
{
	Samurai::IO::Net::InetAddress addr("10.20.30.40");
	return strcmp(addr.toString(), "10.20.30.40") == 0;
});


EXO_TEST(inet_addr_ipv6_basic_1,
{
	Samurai::IO::Net::InetAddress addr("2002::2001:1", Samurai::IO::Net::InetAddress::IPv6);
	return !addr.isMulticast();
});

EXO_TEST(inet_addr_ipv6_basic_2,
{
	Samurai::IO::Net::InetAddress addr("2002::2001:1", Samurai::IO::Net::InetAddress::IPv6);
	return addr.isValid();
});

EXO_TEST(inet_addr_ipv6_basic_3,
{
	Samurai::IO::Net::InetAddress addr("2002::2001:1");
	return addr.getType() == Samurai::IO::Net::InetAddress::IPv6;
});

EXO_TEST(inet_addr_ipv6_basic_4,
{
	Samurai::IO::Net::InetAddress addr("[fe80::201:2ff:fefa:f34e]");
	return addr.isValid();
});

EXO_TEST(inet_addr_ipv6_basic_5,
{
	Samurai::IO::Net::InetAddress addr("fe80::201:2ff:fefa:f34e");
	return addr.getType() != Samurai::IO::Net::InetAddress::IPv4;
});

EXO_TEST(inet_addr_ipv6_basic_6,
{
	Samurai::IO::Net::InetAddress addr("fe80::201:2ff:fefa:f34e");
	return addr.getType() == Samurai::IO::Net::InetAddress::IPv6;
});

EXO_TEST(inet_addr_ipv6_basic_7,
{
	Samurai::IO::Net::InetAddress addr("2001:0db8:85a3:08d3:1319:8a2e:0370:7334");
	return addr.isValid();
});


EXO_TEST(inet_addr_ipv4_detection_1,
{
	Samurai::IO::Net::InetAddress addr("10.21.43.9");
	return addr.getType() == Samurai::IO::Net::InetAddress::IPv4;
});

EXO_TEST(inet_addr_ipv4_detection_2,
{
	Samurai::IO::Net::InetAddress addr1("10.21.43.9");
	Samurai::IO::Net::InetAddress addr2("10.21.43.9", Samurai::IO::Net::InetAddress::IPv4);
	return addr1 == addr2;
});

EXO_TEST(inet_addr_ipv6_detection_1,
{
	Samurai::IO::Net::InetAddress addr("fe80::201:2ff:fefa:f34e");
	return addr.getType() == Samurai::IO::Net::InetAddress::IPv6;
});

EXO_TEST(inet_addr_ipv6_detection_2,
{
	Samurai::IO::Net::InetAddress addr1("fe80::201:2ff:fefa:f34e");
	Samurai::IO::Net::InetAddress addr2("fe80::201:2ff:fefa:f34e", Samurai::IO::Net::InetAddress::IPv6);
	return addr1 == addr2;
});

EXO_TEST(inet_addr_ipv4_loopback_1,
{
	Samurai::IO::Net::InetAddress addr("127.0.0.1", Samurai::IO::Net::InetAddress::IPv4);
	return addr.isLoopback();
});

EXO_TEST(inet_addr_ipv4_loopback_2,
{
	Samurai::IO::Net::InetAddress addr("127.0.0.1", Samurai::IO::Net::InetAddress::IPv4);
	return addr.isValid() && addr.getType() == Samurai::IO::Net::InetAddress::IPv4;
});

EXO_TEST(inet_addr_ipv4_loopback_3,
{
	Samurai::IO::Net::InetAddress addr("127.0.0.1", Samurai::IO::Net::InetAddress::IPv4);
	return strcmp(addr.toString(), "127.0.0.1") == 0;
});

EXO_TEST(inet_addr_ipv4_loopback_4,
{
	Samurai::IO::Net::InetAddress addr("127.25.73.11");
	return addr.isLoopback();
});

EXO_TEST(inet_addr_ipv4_loopback_5,
{
	Samurai::IO::Net::InetAddress addr("192.168.0.1", Samurai::IO::Net::InetAddress::IPv4);
	return !addr.isLoopback();
});




EXO_TEST(inet_addr_ipv6_loopback_1,
{
	Samurai::IO::Net::InetAddress addr("::1", Samurai::IO::Net::InetAddress::IPv6);
	return addr.isLoopback();
});

EXO_TEST(inet_addr_ipv6_loopback_2,
{
	Samurai::IO::Net::InetAddress addr("::1", Samurai::IO::Net::InetAddress::IPv6);
	return addr.isValid();
});

EXO_TEST(inet_addr_ipv6_loopback_3,
{
	Samurai::IO::Net::InetAddress addr("::1", Samurai::IO::Net::InetAddress::IPv6);
	if (addr.toString())
	return strcmp(addr.toString(), "::1") == 0;
});

EXO_TEST(inet_addr_ipv6_loopback_4,
{
	Samurai::IO::Net::InetAddress addr("fe80::201:2ff:fefa:f34e", Samurai::IO::Net::InetAddress::IPv6);
	return !addr.isLoopback();
});




EXO_TEST(inet_addr_ipv4_private_1,
{
	Samurai::IO::Net::InetAddress addr("192.168.0.0", Samurai::IO::Net::InetAddress::IPv4);
	return addr.isPrivate();
});

EXO_TEST(inet_addr_ipv4_private_2,
{
	Samurai::IO::Net::InetAddress addr("192.168.123.45", Samurai::IO::Net::InetAddress::IPv4);
	return addr.isPrivate();
});

EXO_TEST(inet_addr_ipv4_private_3,
{
	Samurai::IO::Net::InetAddress addr("192.168.255.255", Samurai::IO::Net::InetAddress::IPv4);
	return addr.isPrivate();
});

EXO_TEST(inet_addr_ipv4_private_4,
{
	Samurai::IO::Net::InetAddress addr("172.16.0.0", Samurai::IO::Net::InetAddress::IPv4);
	return addr.isPrivate();
});

EXO_TEST(inet_addr_ipv4_private_5,
{
	Samurai::IO::Net::InetAddress addr("172.17.12.51", Samurai::IO::Net::InetAddress::IPv4);
	return addr.isPrivate();
});

EXO_TEST(inet_addr_ipv4_private_6,
{
	Samurai::IO::Net::InetAddress addr("172.19.255.255", Samurai::IO::Net::InetAddress::IPv4);
	return addr.isPrivate();
});

EXO_TEST(inet_addr_ipv4_private_7,
{
	Samurai::IO::Net::InetAddress addr("10.0.0.0", Samurai::IO::Net::InetAddress::IPv4);
	return addr.isPrivate();
});

EXO_TEST(inet_addr_ipv4_private_8,
{
	Samurai::IO::Net::InetAddress addr("10.71.1.26", Samurai::IO::Net::InetAddress::IPv4);
	return addr.isPrivate();
});

EXO_TEST(inet_addr_ipv4_private_9,
{
	Samurai::IO::Net::InetAddress addr("10.255.255.255", Samurai::IO::Net::InetAddress::IPv4);
	return addr.isPrivate();
});

EXO_TEST(inet_addr_ipv4_private_10,
{
	Samurai::IO::Net::InetAddress addr("8.24.15.23", Samurai::IO::Net::InetAddress::IPv4);
	return !addr.isPrivate();
});

EXO_TEST(inet_addr_ipv4_private_11,
{
	Samurai::IO::Net::InetAddress addr("80.124.5.171", Samurai::IO::Net::InetAddress::IPv4);
	return !addr.isPrivate();
});

EXO_TEST(inet_addr_ipv4_private_12,
{
	Samurai::IO::Net::InetAddress addr("193.102.12.11", Samurai::IO::Net::InetAddress::IPv4);
	return !addr.isPrivate();
});

EXO_TEST(inet_addr_ipv4_private_13,
{
	Samurai::IO::Net::InetAddress addr("222.255.255.255", Samurai::IO::Net::InetAddress::IPv4);
	return !addr.isPrivate();
});

EXO_TEST(inet_addr_ipv4_private_14,
{
	Samurai::IO::Net::InetAddress addr("240.1.2.3", Samurai::IO::Net::InetAddress::IPv4);
	return !addr.isPrivate();
});


EXO_TEST(inet_addr_ipv4_multicast_1,
{
	Samurai::IO::Net::InetAddress addr("227.12.92.16", Samurai::IO::Net::InetAddress::IPv4);
	return addr.isMulticast();
});

EXO_TEST(inet_addr_ipv4_multicast_2,
{
	Samurai::IO::Net::InetAddress addr("224.0.0.0", Samurai::IO::Net::InetAddress::IPv4);
	return addr.isMulticast();
});

EXO_TEST(inet_addr_ipv4_multicast_3,
{
	Samurai::IO::Net::InetAddress addr("239.255.255.255", Samurai::IO::Net::InetAddress::IPv4);
	return addr.isMulticast();
});

EXO_TEST(inet_addr_ipv4_multicast_4,
{
	Samurai::IO::Net::InetAddress addr("240.0.0.0", Samurai::IO::Net::InetAddress::IPv4);
	return !addr.isMulticast();
});

EXO_TEST(inet_addr_ipv4_multicast_5,
{
	Samurai::IO::Net::InetAddress addr("8.24.15.23", Samurai::IO::Net::InetAddress::IPv4);
	return !addr.isMulticast();
});

EXO_TEST(inet_addr_ipv4_compare_1,
{
	Samurai::IO::Net::InetAddress addr1("1.2.3.4", Samurai::IO::Net::InetAddress::IPv4);
	Samurai::IO::Net::InetAddress addr2("1.2.3.4", Samurai::IO::Net::InetAddress::IPv4);
	return addr1 == addr2;
});

EXO_TEST(inet_addr_ipv4_compare_2,
{
	Samurai::IO::Net::InetAddress addr1("1.2.3.4", Samurai::IO::Net::InetAddress::IPv4);
	Samurai::IO::Net::InetAddress addr2("1.2.3.4", Samurai::IO::Net::InetAddress::IPv4);
	return !(addr1 != addr2);
});


EXO_TEST(inet_addr_ipv4_compare_3,
{
	Samurai::IO::Net::InetAddress addr1("1.2.3.4", Samurai::IO::Net::InetAddress::IPv4);
	Samurai::IO::Net::InetAddress addr2("4.3.2.1", Samurai::IO::Net::InetAddress::IPv4);
	return addr1 != addr2;
});

EXO_TEST(inet_addr_ipv4_compare_4,
{
	Samurai::IO::Net::InetAddress addr1("1.2.3.4", Samurai::IO::Net::InetAddress::IPv4);
	Samurai::IO::Net::InetAddress addr2("4.3.2.1", Samurai::IO::Net::InetAddress::IPv4);
	return !(addr1 == addr2);
});

EXO_TEST(inet_addr_ipv6_compare_1,
{
	Samurai::IO::Net::InetAddress addr1("fe80::201:2ff:fefa:f34e", Samurai::IO::Net::InetAddress::IPv6);
	Samurai::IO::Net::InetAddress addr2("fe80::201:2ff:fefa:f34e", Samurai::IO::Net::InetAddress::IPv6);
	return addr1 == addr2;
});

EXO_TEST(inet_addr_ipv6_compare_2,
{
	Samurai::IO::Net::InetAddress addr1("fe80::201:2ff:fefa:f34e", Samurai::IO::Net::InetAddress::IPv6);
	Samurai::IO::Net::InetAddress addr2("fe80::201:2ff:fefa:f34e", Samurai::IO::Net::InetAddress::IPv6);
	return !(addr1 != addr2);
});


EXO_TEST(inet_addr_ipv6_compare_3,
{
	Samurai::IO::Net::InetAddress addr1("fe80::201:2ff:fefa:f34e", Samurai::IO::Net::InetAddress::IPv6);
	Samurai::IO::Net::InetAddress addr2("2002::2001:1", Samurai::IO::Net::InetAddress::IPv6);
	return addr1 != addr2;
});

EXO_TEST(inet_addr_ipv6_compare_4,
{
	Samurai::IO::Net::InetAddress addr1("fe80::201:2ff:fefa:f34e", Samurai::IO::Net::InetAddress::IPv6);
	Samurai::IO::Net::InetAddress addr2("2002::2001:1", Samurai::IO::Net::InetAddress::IPv6);
	return !(addr1 == addr2);
});

EXO_TEST(inet_addr_ipv6_compare_5,
{
	Samurai::IO::Net::InetAddress addr1("fe80:0:0:0:201:0:0:f34e", Samurai::IO::Net::InetAddress::IPv6);
	Samurai::IO::Net::InetAddress addr2("fe80::201:0:0:f34e", Samurai::IO::Net::InetAddress::IPv6);
	return addr1 == addr2;
});

EXO_TEST(inet_addr_ipv6_compare_6,
{
	Samurai::IO::Net::InetAddress addr1("2001:0db8:0000:0000:0000:0000:1428:57ab");
	Samurai::IO::Net::InetAddress addr2("2001:0db8:0000:0000:0000::1428:57ab");
	Samurai::IO::Net::InetAddress addr3("2001:0db8:0:0:0:0:1428:57ab");
	Samurai::IO::Net::InetAddress addr4("2001:0db8:0:0::1428:57ab");
	Samurai::IO::Net::InetAddress addr5("2001:0db8::1428:57ab");
	Samurai::IO::Net::InetAddress addr6("2001:db8::1428:57ab");
	return addr1 == addr2 && addr1 == addr3 && addr1 == addr4 && addr1 == addr5 && addr1 == addr6 && addr1 == addr1;
});

EXO_TEST(inet_addr_ipv6_compare_7,
{
	Samurai::IO::Net::InetAddress addr1("::ffff:1.2.3.4");
	Samurai::IO::Net::InetAddress addr2("::ffff:0102:0304");
	return addr1 == addr2;
});

EXO_TEST(inet_addr_ipv6_compare_8,
{
	Samurai::IO::Net::InetAddress addr1("::ffff:15.16.18.31");
	Samurai::IO::Net::InetAddress addr2("::ffff:0f10:121f");
	return addr1 == addr2;
});

