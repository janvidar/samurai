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
	return url.getScheme() == "ftp"
		&& url.getUsername() == "user"
		&& url.getPassword().empty()
		&& url.getPort() == 8080
		&& url.getPath() == "/file";
});

EXO_TEST(url_parse_20,
{
	Samurai::IO::Net::URL url("ftp://user:pass@foo:8080/file");
	return url.getScheme() == "ftp"
		&& url.getUsername() == "user"
		&& url.getPassword() == "pass"
		&& url.getPort() == 8080
		&& url.getPath() == "/file";
});

/* An empty userinfo leaves both fields empty rather than borrowing the host. */
EXO_TEST(url_parse_21,
{
	Samurai::IO::Net::URL url("ftp://@foo:8080/file");
	return url.getScheme() == "ftp"
		&& url.getUsername().empty()
		&& url.getPassword().empty()
		&& url.getPort() == 8080;
});

EXO_TEST(url_parse_22,
{
	Samurai::IO::Net::URL url("ftp://:@foo:8080/file");
	return url.getScheme() == "ftp"
		&& url.getUsername().empty()
		&& url.getPassword().empty()
		&& url.getPort() == 8080;
});

EXO_TEST(url_parse_23,
{
	Samurai::IO::Net::URL url("ftp://blankpass:@foo:8080/file");
	return url.getScheme() == "ftp"
		&& url.getUsername() == "blankpass"
		&& url.getPassword().empty()
		&& url.getPort() == 8080;
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


/* ------------------------------------------------------------------------- */
/* Path, file and query                                                      */
/*                                                                           */
/* getPath() is the path alone; getFile() is the whole request target, so it  */
/* carries the query string with it. The two differ only when there is one.   */
/* ------------------------------------------------------------------------- */

EXO_TEST(url_path_and_query_are_split,
{
	Samurai::IO::Net::URL url("http://example.com/a/b/c.html?x=1&y=2");
	return url.getPath() == "/a/b/c.html"
		&& url.getQuery() == "x=1&y=2"
		&& url.getFile() == "/a/b/c.html?x=1&y=2";
});

EXO_TEST(url_without_query_has_none,
{
	Samurai::IO::Net::URL url("http://example.com/a/b/c.html");
	return url.getPath() == "/a/b/c.html"
		&& url.getQuery().empty()
		&& url.getFile() == "/a/b/c.html";
});

/* A missing path is the root, not the empty string. */
EXO_TEST(url_absent_path_is_root,
{
	Samurai::IO::Net::URL bare("http://example.com");
	Samurai::IO::Net::URL slash("http://example.com/");
	return bare.getPath() == "/" && slash.getPath() == "/";
});

EXO_TEST(url_empty_query_after_question_mark,
{
	Samurai::IO::Net::URL url("http://example.com/a?");
	return url.getPath() == "/a" && url.getQuery().empty();
});

/* No default port is inferred from the scheme; an unspecified port reads 0. */
EXO_TEST(url_unspecified_port_is_zero,
{
	Samurai::IO::Net::URL url("http://example.com/a");
	return url.getPort() == 0;
});

/* ------------------------------------------------------------------------- */
/* toString, equality and copying                                            */
/* ------------------------------------------------------------------------- */

EXO_TEST(url_tostring_round_trips,
{
	const char* text = "http://example.com/a/b.html?x=1";
	Samurai::IO::Net::URL url(text);
	return url.toString() == text;
});

EXO_TEST(url_tostring_keeps_credentials_and_port,
{
	const char* text = "ftp://user:pass@foo:8080/file";
	Samurai::IO::Net::URL url(text);
	return url.toString() == text;
});

EXO_TEST(url_tostring_reparses_to_an_equal_url,
{
	Samurai::IO::Net::URL url("adc://hub.example.com:411");
	Samurai::IO::Net::URL again(url.toString());
	return again == url;
});

EXO_TEST(url_equality_compares_the_whole_url,
{
	Samurai::IO::Net::URL a("http://example.com/x");
	Samurai::IO::Net::URL b("http://example.com/x");
	Samurai::IO::Net::URL c("http://example.com/y");
	return a == b && !(a == c) && a != c && !(a != b);
});

EXO_TEST(url_copy_construction_preserves_every_field,
{
	Samurai::IO::Net::URL original("ftp://user:pass@foo:8080/dir/file?q=1");
	Samurai::IO::Net::URL copy(original);

	return copy == original
		&& copy.getScheme() == original.getScheme()
		&& copy.getUsername() == original.getUsername()
		&& copy.getPassword() == original.getPassword()
		&& copy.getPort() == original.getPort()
		&& copy.getPath() == original.getPath()
		&& copy.getQuery() == original.getQuery();
});

EXO_TEST(url_assignment_replaces_every_field,
{
	Samurai::IO::Net::URL target("http://other.example/z");
	Samurai::IO::Net::URL source("ftp://user:pass@foo:8080/dir/file?q=1");

	target = source;
	return target == source
		&& target.getUsername() == "user"
		&& target.getPath() == "/dir/file"
		&& target.getQuery() == "q=1";
});

EXO_TEST(url_self_assignment_is_safe,
{
	Samurai::IO::Net::URL url("ftp://user:pass@foo:8080/file");
	url = url;
	return url.getUsername() == "user"
		&& url.getPassword() == "pass"
		&& url.getPath() == "/file";
});

EXO_TEST(url_invalid_has_no_scheme,
{
	Samurai::IO::Net::URL url("not a url");
	return !url.isValid() && url.getScheme().empty();
});

/* An address literal is a peer name too - it is what a TLS client verifies an
   iPAddress SAN against - so it has to survive URL parsing. */
EXO_TEST(url_host_keeps_an_ipv4_literal,
{
	Samurai::IO::Net::URL url("http://127.0.0.1/index.html");
	return url.isValid() && url.getHost().getHostname() == "127.0.0.1";
});

EXO_TEST(url_host_keeps_an_ipv6_literal_without_brackets,
{
	Samurai::IO::Net::URL url("http://[::1]:8080/index.html");
	return url.isValid() && url.getHost().getHostname() == "::1";
});
