/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/dns/common.h>

#ifndef HAVE_SYSTEM_DNS_UTILS_H
#define HAVE_SYSTEM_DNS_UTILS_H

namespace Samurai {
namespace IO {
namespace Net {
namespace DNS {

class Validator {
	public:
		/**
		 * Checks if a (fully qualified) hostname is valid.
		 * Validity checks consists of checking for illegal characters,
		 * and malformed syntax (double periods, etc).
		 *
 		 * A name consists of multiple labels joined by ".".
		 * Example: 'www.example.com'
 		 */
		static bool isValidName(const char* buf, size_t len);

		/**
		 * Checks if a label is valid (does not contain illegal characters).
		 *
		 * A label is part of a fully qualified hostname.
		 * Example: 'www.example.com" consists of three labels;
		 * 'www', 'example' and 'com'.
		 */
		static bool isValidLabel(const char* buf, size_t len);
};

class Label {
	public:
		Label(const char* val, uint8_t sz);
		Label(const Label& copy);
		Label(Label* copy);
		virtual ~Label();
		
		bool isValid();
		const char* getName() const;
		uint8_t getSize() const;
		
		bool operator==(const Label& label);
		bool operator!=(const Label& label);
		

	protected:
		char   name[DNS_LABEL_SIZE+1];
		uint8_t size;
};


class Name {
	public:
		Name();
		Name(const Name& copy);
		Name(const char* hostname);
		virtual ~Name();

		int split();
		bool isValid();
		bool join();
		uint8_t countParts() const;
		void addPart(Label* label);
		char* toString();
		void clear();
		
		bool operator==(const Name& name);
		bool operator!=(const Name& name);
		
	public:
		char name[DNS_NAME_SIZE+1];
		size_t size;
		size_t offset;
		mutable std::vector<Label*> parts;
		
};


}
}
}
}

#endif // HAVE_SYSTEM_DNS_UTILS_H

