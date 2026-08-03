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

/* ------------------------------------------------------------------------- */
/* Effective port                                                            */
/*                                                                           */
/* getPort() still answers "what the URL states", 0 when it states none, so   */
/* everything above keeps its meaning. getEffectivePort() folds in the        */
/* scheme's default, which is what a caller about to connect wants.           */
/* ------------------------------------------------------------------------- */

EXO_TEST(url_effective_port_defaults_to_80_for_http,
{
	Samurai::IO::Net::URL url("http://192.168.1.1/rootDesc.xml");
	return url.isValid() && url.getPort() == 0 && url.getEffectivePort() == 80;
});

EXO_TEST(url_effective_port_defaults_to_443_for_https,
{
	Samurai::IO::Net::URL url("https://example.org/");
	return url.getEffectivePort() == 443;
});

EXO_TEST(url_effective_port_prefers_an_explicit_port,
{
	Samurai::IO::Net::URL url("http://192.168.1.1:5000/rootDesc.xml");
	return url.getPort() == 5000 && url.getEffectivePort() == 5000;
});

EXO_TEST(url_effective_port_is_zero_for_an_unknown_scheme,
{
	Samurai::IO::Net::URL url("gopher://example.org/");
	return url.isValid() && url.getEffectivePort() == 0;
});

EXO_TEST(url_effective_port_is_zero_for_an_invalid_url,
{
	Samurai::IO::Net::URL url("not a url");
	return !url.isValid() && url.getEffectivePort() == 0;
});

EXO_TEST(url_default_port_is_case_insensitive,
{
	return Samurai::IO::Net::URL::getDefaultPort("HTTP") == 80
		&& Samurai::IO::Net::URL::getDefaultPort("Https") == 443
		&& Samurai::IO::Net::URL::getDefaultPort("nonesuch") == 0;
});

/* ------------------------------------------------------------------------- */
/* Hostname as text                                                          */
/* ------------------------------------------------------------------------- */

EXO_TEST(url_hostname_of_a_name,
{
	Samurai::IO::Net::URL url("http://router.local/x");
	return url.getHostname() == "router.local";
});

EXO_TEST(url_hostname_of_an_ipv6_literal_has_no_brackets,
{
	Samurai::IO::Net::URL url("http://[fe80::1]:5000/x");
	return url.getHostname() == "fe80::1";
});

EXO_TEST(url_hostname_is_empty_when_there_is_no_authority,
{
	Samurai::IO::Net::URL url("file:///tmp/x");
	return url.isValid() && url.getHostname().empty();
});

/* ------------------------------------------------------------------------- */
/* IPv6 zone identifier                                                      */
/*                                                                           */
/* A zone belongs to the interface an address is reached by, not to the       */
/* address, so it is split off rather than handed to the literal parser -     */
/* which would reject it.                                                    */
/* ------------------------------------------------------------------------- */

EXO_TEST(url_ipv6_without_a_zone_has_none,
{
	Samurai::IO::Net::URL url("http://[fe80::1]:5000/x");
	return url.isValid() && url.getZoneId().empty();
});

/* RFC 6874 requires the delimiter to be written "%25" inside a URI. */
EXO_TEST(url_ipv6_percent_encoded_zone_is_decoded,
{
	Samurai::IO::Net::URL url("http://[fe80::1%25en0]:5000/rootDesc.xml");
	return url.isValid()
		&& url.getHostname() == "fe80::1"
		&& url.getZoneId() == "en0"
		&& url.getPort() == 5000;
});

/* Devices write the bare form too, so it is accepted. */
EXO_TEST(url_ipv6_bare_zone_is_accepted,
{
	Samurai::IO::Net::URL url("http://[fe80::1%en0]/x");
	return url.isValid() && url.getHostname() == "fe80::1" && url.getZoneId() == "en0";
});

EXO_TEST(url_ipv6_numeric_zone_is_accepted,
{
	Samurai::IO::Net::URL url("http://[fe80::1%253]/x");
	return url.isValid() && url.getZoneId() == "3";
});

EXO_TEST(url_ipv6_empty_zone_is_refused,
{
	Samurai::IO::Net::URL url("http://[fe80::1%25]/x");
	return !url.isValid();
});

EXO_TEST(url_ipv6_zone_without_an_address_is_refused,
{
	Samurai::IO::Net::URL url("http://[%25en0]/x");
	return !url.isValid();
});

/* A bare '%' before a number is a zone index, not a truncated escape: that is
   the form getaddrinfo() takes, and an interface index is what it names. */
EXO_TEST(url_ipv6_bare_numeric_zone_is_an_index,
{
	Samurai::IO::Net::URL url("http://[fe80::1%2]/x");
	return url.isValid() && url.getZoneId() == "2";
});

EXO_TEST(url_ipv6_zone_with_a_bad_escape_is_refused,
{
	Samurai::IO::Net::URL url("http://[fe80::1%25en%0]/x");
	return !url.isValid();
});

EXO_TEST(url_ipv6_zone_survives_reparsing,
{
	Samurai::IO::Net::URL url("http://[fe80::1%25en0]:5000/x");
	Samurai::IO::Net::URL copy = url;
	return copy.isValid() && copy.getZoneId() == "en0" && copy.getHostname() == "fe80::1";
});

/* ------------------------------------------------------------------------- */
/* Relative reference resolution, RFC 3986 section 5.3                       */
/*                                                                           */
/* A UPnP controlURL is normally a path, so it has to be resolved against     */
/* URLBase or the description's own location. The examples below are the      */
/* normal and abnormal tables from RFC 3986 section 5.4, against the base the */
/* RFC uses.                                                                 */
/*                                                                           */
/* One deliberate difference: a fragment is dropped rather than carried, as   */
/* parse() has always done - this class does not model one, and it never      */
/* reaches a request target.                                                 */
/* ------------------------------------------------------------------------- */

namespace {

const char* RFC3986_BASE = "http://a/b/c/d;p?q";

bool resolves(const char* reference, const char* expected)
{
	Samurai::IO::Net::URL base(RFC3986_BASE);
	const Samurai::IO::Net::URL result = base.resolve(reference);
	if (!result.isValid()) return false;

	if (result.toString() != expected)
	{
		printf("resolve(\"%s\") gave \"%s\", wanted \"%s\"\n",
		       reference, result.toString().c_str(), expected);
		return false;
	}
	return true;
}

}

EXO_TEST(url_resolve_rfc3986_normal_examples,
{
	return resolves("g",       "http://a/b/c/g")
		&& resolves("./g",     "http://a/b/c/g")
		&& resolves("g/",      "http://a/b/c/g/")
		&& resolves("/g",      "http://a/g")
		&& resolves("//g",     "http://g")
		&& resolves("?y",      "http://a/b/c/d;p?y")
		&& resolves("g?y",     "http://a/b/c/g?y")
		&& resolves(";x",      "http://a/b/c/;x")
		&& resolves("g;x",     "http://a/b/c/g;x")
		&& resolves("g;x?y",   "http://a/b/c/g;x?y")
		&& resolves("",        "http://a/b/c/d;p?q")
		&& resolves(".",       "http://a/b/c/")
		&& resolves("./",      "http://a/b/c/")
		&& resolves("..",      "http://a/b/")
		&& resolves("../",     "http://a/b/")
		&& resolves("../g",    "http://a/b/g")
		&& resolves("../..",   "http://a/")
		&& resolves("../../",  "http://a/")
		&& resolves("../../g", "http://a/g");
});

/* The abnormal table. A ".." at the root is discarded rather than escaping
   above it, which is what a naive implementation gets wrong. */
EXO_TEST(url_resolve_rfc3986_abnormal_examples,
{
	return resolves("../../../g",    "http://a/g")
		&& resolves("../../../../g", "http://a/g")
		&& resolves("/./g",          "http://a/g")
		&& resolves("/../g",         "http://a/g")
		&& resolves("g.",            "http://a/b/c/g.")
		&& resolves(".g",            "http://a/b/c/.g")
		&& resolves("g..",           "http://a/b/c/g..")
		&& resolves("..g",           "http://a/b/c/..g")
		&& resolves("./../g",        "http://a/b/g")
		&& resolves("./g/.",         "http://a/b/c/g/")
		&& resolves("g/./h",         "http://a/b/c/g/h")
		&& resolves("g/../h",        "http://a/b/c/h")
		&& resolves("g;x=1/./y",     "http://a/b/c/g;x=1/y")
		&& resolves("g;x=1/../y",    "http://a/b/c/y");
});

EXO_TEST(url_resolve_drops_a_fragment,
{
	return resolves("g#s", "http://a/b/c/g");
});

EXO_TEST(url_resolve_absolute_reference_replaces_everything,
{
	Samurai::IO::Net::URL base(RFC3986_BASE);
	const Samurai::IO::Net::URL result = base.resolve("https://other:81/x?z");
	return result.isValid()
		&& result.getScheme() == "https"
		&& result.getHostname() == "other"
		&& result.getPort() == 81
		&& result.getPath() == "/x";
});

EXO_TEST(url_resolve_of_an_invalid_base_is_invalid,
{
	Samurai::IO::Net::URL base("not a url");
	return !base.resolve("/ctl/IPConn").isValid();
});

/* ------------------------------------------------------------------------- */
/* The shapes a UPnP device description actually produces                     */
/* ------------------------------------------------------------------------- */

EXO_TEST(url_resolve_an_absolute_control_path,
{
	Samurai::IO::Net::URL location("http://192.168.1.1:5000/rootDesc.xml");
	const Samurai::IO::Net::URL control = location.resolve("/ctl/IPConn");
	return control.isValid()
		&& control.toString() == "http://192.168.1.1:5000/ctl/IPConn"
		&& control.getEffectivePort() == 5000;
});

EXO_TEST(url_resolve_a_relative_control_path,
{
	Samurai::IO::Net::URL location("http://192.168.1.1:5000/upnp/rootDesc.xml");
	return location.resolve("ctl/IPConn").toString()
		== "http://192.168.1.1:5000/upnp/ctl/IPConn";
});

/* A URLBase with a trailing slash against a controlURL with a leading one must
   not produce a doubled slash: routers send exactly this pair. */
EXO_TEST(url_resolve_does_not_double_a_slash,
{
	Samurai::IO::Net::URL base("http://192.168.1.1:5000/");
	return base.resolve("/ctl/IPConn").toString()
		== "http://192.168.1.1:5000/ctl/IPConn";
});

EXO_TEST(url_resolve_against_a_base_with_no_path,
{
	Samurai::IO::Net::URL base("http://192.168.1.1:5000");
	return base.resolve("/ctl/IPConn").toString()
		== "http://192.168.1.1:5000/ctl/IPConn";
});

EXO_TEST(url_resolve_keeps_an_ipv6_literal_bracketed,
{
	Samurai::IO::Net::URL location("http://[fe80::1]:5000/rootDesc.xml");
	const Samurai::IO::Net::URL control = location.resolve("/ctl/IPConn");
	return control.isValid()
		&& control.getHostname() == "fe80::1"
		&& control.toString() == "http://[fe80::1]:5000/ctl/IPConn";
});

EXO_TEST(url_resolve_keeps_an_ipv6_zone,
{
	Samurai::IO::Net::URL location("http://[fe80::1%25en0]:5000/rootDesc.xml");
	const Samurai::IO::Net::URL control = location.resolve("/ctl/IPConn");
	return control.isValid()
		&& control.getZoneId() == "en0"
		&& control.getHostname() == "fe80::1";
});
