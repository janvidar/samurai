/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_URL_H
#define HAVE_SAMURAI_URL_H

#include <string>
#include <string_view>
#include <samurai/io/net/inetaddress.h>

namespace Samurai {
namespace IO {
namespace Net {
	
class URL {

	public:
		URL(const char* cstr);
		URL(const std::string& str);
		URL(const URL& url);
		URL(URL* url);
		~URL();
		
		Samurai::IO::Net::InetAddress getHost() const { return host; }
		uint16_t getPort() const { return port; }
		std::string getScheme() const { return scheme; }
		std::string getFile() const { return file; }
		std::string getPath() const { return path; }
		std::string getQuery() const { return query; }
		std::string getUsername() const { return username; }
		std::string getPassword() const { return password; }
		std::string toString() const;
		URL& operator=(const URL& u);

		/**
		 * The port to connect to: the one the URL states, or the default
		 * registered for its scheme, or 0 when the scheme has no well known
		 * one.
		 *
		 * getPort() keeps its own meaning - the port the URL states, and 0 when
		 * it states none - because a caller sometimes has to tell an explicit
		 * ":80" from an absent port, and cannot once a default has been folded
		 * in.
		 */
		uint16_t getEffectivePort() const;

		/** The well known port for a scheme, or 0 if it has none. */
		static uint16_t getDefaultPort(std::string_view scheme);

		/**
		 * The host as text: a name as it was written, or an Version::IPv6
		 * literal without its brackets and without its zone identifier.
		 *
		 * getHost() returns an InetAddress, which for a name is only a
		 * container for the text, and which cannot be written into a Host:
		 * header without deciding about the brackets again.
		 */
		std::string getHostname() const;

		/**
		 * The Version::IPv6 zone identifier from the host, percent-decoded, or
		 * empty when there is none.
		 *
		 * RFC 6874 requires the '%' to be written "%25" inside a URI. Both
		 * spellings are accepted, because devices send both.
		 */
		std::string getZoneId() const { return zone; }

		/**
		 * Resolve a reference against this URL, as RFC 3986 section 5.3
		 * defines it.
		 *
		 * @return a URL whose isValid() is false when this URL is not itself
		 *         valid, or the result cannot be formed.
		 */
		URL resolve(std::string_view reference) const;

		bool isValid() const;

		bool operator==(const URL& u) const;
		bool operator!=(const URL& u) const;

	protected:
		std::string url;
		std::string scheme;
		Samurai::IO::Net::InetAddress host;
		/* The host as written, which getHost() cannot give back: an InetAddress
		   holding a name reports the name, but one holding a literal reformats
		   it. */
		std::string hostname;
		/* The Version::IPv6 zone identifier, decoded, empty when absent. */
		std::string zone;
		uint16_t port;
		std::string path;
		std::string username;
		std::string password;

		std::string file;
		std::string query;
		bool valid;
		void parse();
};

}
}
}

#endif // HAVE_SAMURAI_URL_H

