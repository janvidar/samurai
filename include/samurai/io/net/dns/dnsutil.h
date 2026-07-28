/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SYSTEM_DNS_UTILS_H
#define HAVE_SYSTEM_DNS_UTILS_H

#include <samurai/samurai.h>
#include <samurai/io/net/dns/common.h>
#include <vector>
#include <string_view>

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
		static bool isValidName(std::string_view name);

		/**
		 * Checks if a label is valid (does not contain illegal characters).
		 *
		 * A label is part of a fully qualified hostname.
		 * Example: 'www.example.com" consists of three labels;
		 * 'www', 'example' and 'com'.
		 */
		static bool isValidLabel(std::string_view label);
};

class Label {
	public:
		Label(const char* val, uint8_t sz);
		Label(const Label& copy);
		Label(Label* copy);
		~Label();
		
		bool isValid();
		const char* getName() const;
		uint8_t getSize() const;
		
		bool operator==(const Label& label) const;
		bool operator!=(const Label& label) const;


	protected:
		char   name[DNS_LABEL_SIZE+1];
		uint8_t size;
};


class Name {
	public:
		Name();
		Name(const Name& copy) = default;
		Name& operator=(const Name& copy) = default;
		Name(const char* hostname);
		~Name() = default;

		int split();
		bool isValid();
		bool join();
		uint8_t countParts() const;
		void addPart(const Label& label);
		std::string toString() const;
		void clear();
		
		bool operator==(const Name& name) const;
		bool operator!=(const Name& name) const;
		
	public:
		char name[DNS_NAME_SIZE+1];
		size_t size;
		size_t offset;
		/* Held by value: a Label is a fixed char array and a length, with no
		   pointers of its own, so there is nothing to allocate and Name gets
		   correct copying for free. */
		std::vector<Label> parts;
		
};


}
}
}
}

#endif // HAVE_SYSTEM_DNS_UTILS_H

