#include <samurai/io/net/url.h>
#include <stdio.h>

/* BASIC TESTS */

EXO_TEST(url_parse_1,
{
	Samurai::IO::Net::URL url("http://foo/");
	return url.isValid();
});

EXO_TEST(url_parse_2,
{
	Samurai::IO::Net::URL url("http://");
	return url.isValid();
});

EXO_TEST(url_parse_3,
{
	Samurai::IO::Net::URL url("http://foo:");
	return !url.isValid();
});

EXO_TEST(url_parse_4,
{
	Samurai::IO::Net::URL url("http://foo:80");
	return url.isValid();
});

EXO_TEST(url_parse_5,
{
	Samurai::IO::Net::URL url("http://foo:80/file");
	return url.isValid();
});

EXO_TEST(url_parse_6,
{
	Samurai::IO::Net::URL url("http://foo:80000/file");
	return !url.isValid();
});

EXO_TEST(url_parse_7,
{
	Samurai::IO::Net::URL url("http://foo:0/file");
	return !url.isValid();
});

EXO_TEST(url_parse_8,
{
	Samurai::IO::Net::URL url("://foo/file");
	return !url.isValid();
});

EXO_TEST(url_parse_9,
{
	Samurai::IO::Net::URL url("http:/foo");
	return !url.isValid();
});

EXO_TEST(url_parse_11,
{
	Samurai::IO::Net::URL url("file://localhost/file");
	return url.isValid();
});

EXO_TEST(url_parse_12,
{
	Samurai::IO::Net::URL url("file:///file");
	return url.isValid();
});

EXO_TEST(url_parse_13,
{
	Samurai::IO::Net::URL url("http://foo:80/file");
	return url.getHost().getHostname() == "foo";
});

EXO_TEST(url_parse_14,
{
	Samurai::IO::Net::URL url("http://foo.example.com:80/file");
	return url.getHost().getHostname() == "foo.example.com";
});

EXO_TEST(url_parse_15,
{
	Samurai::IO::Net::URL url("http://foo:8080/file");
	return url.getPort() == 8080;
});

EXO_TEST(url_parse_16,
{
	Samurai::IO::Net::URL url("http://foo:8080/file");
	return url.getScheme() == "http";
});

EXO_TEST(url_parse_17,
{
	Samurai::IO::Net::URL url("ftp://foo:8080/file");
	return url.getScheme() == "ftp";
});

EXO_TEST(url_parse_18,
{
	Samurai::IO::Net::URL url("protocolspec://hostname.verylong.example.com:8080/path/to/file?query");
	return url.getScheme() == "protocolspec";
});

EXO_TEST(url_parse_19,
{
	Samurai::IO::Net::URL url("ftp://user@foo:8080/file");
	return url.getScheme() == "ftp";
});

EXO_TEST(url_parse_20,
{
	Samurai::IO::Net::URL url("ftp://user:pass@foo:8080/file");
	return url.getScheme() == "ftp";
});

EXO_TEST(url_parse_21,
{
	Samurai::IO::Net::URL url("ftp://@foo:8080/file");
	return url.getScheme() == "ftp";
});

EXO_TEST(url_parse_22,
{
	Samurai::IO::Net::URL url("ftp://:@foo:8080/file");
	return url.getScheme() == "ftp";
});

EXO_TEST(url_parse_23,
{
	Samurai::IO::Net::URL url("ftp://blankpass:@foo:8080/file");
	return url.getScheme() == "ftp";
});

EXO_TEST(url_parse_24,
{
	Samurai::IO::Net::URL url("ftp://127.0.0.1:1234");
	return url.getScheme() == "ftp";
});

EXO_TEST(url_parse_25,
{
	Samurai::IO::Net::URL url("http://[::1]");
	return url.isValid();
});

EXO_TEST(url_parse_26,
{
	Samurai::IO::Net::URL url("http://[::1]:8080");
	return url.isValid();
});

EXO_TEST(url_parse_27,
{
	Samurai::IO::Net::URL url("http://[::1]:8080/file?query");
	return url.isValid();
});

EXO_TEST(url_parse_28,
{
	Samurai::IO::Net::URL url("http://foo:8000/");
	return url.getPort() == 8000;
});

EXO_TEST(url_parse_29,
{
	Samurai::IO::Net::URL url("http://foo");
	return url.getPort() == 0;
});

EXO_TEST(url_parse_30,
{
	Samurai::IO::Net::URL url("http://foo/file.txt");
	return url.getFile() == "/file.txt";
});

EXO_TEST(url_parse_31,
{
	Samurai::IO::Net::URL url("http://foo/");
	return url.getFile() == "/";
});

EXO_TEST(url_parse_32,
{
	Samurai::IO::Net::URL url("http://foo/");
	return url.getHost().getHostname() == "foo";
});

