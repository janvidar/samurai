/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_URL_H
#define HAVE_SAMURAI_URL_H

#include <string>
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
		virtual ~URL();
		
		Samurai::IO::Net::InetAddress getHost() const { return host; }
		uint16_t getPort() const { return port; }
		std::string getScheme() const { return scheme; }
		std::string getFile() const { return file; }
		std::string getPath() const { return path; }
		std::string getQuery() const { return query; }
		std::string getUsername() const { return username; }
		std::string getPassword() const { return password; }
		std::string toString();
		URL& operator=(const URL& u);
		
		bool isValid() const;
		
		bool operator==(const URL& u) const;
		bool operator!=(const URL& u) const;

	protected:
		std::string url;
		std::string scheme;
		Samurai::IO::Net::InetAddress host;
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

