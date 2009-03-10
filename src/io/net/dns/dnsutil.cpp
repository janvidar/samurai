/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/dns/dnsutil.h>

// FIXME: Handle "_" (underscore) correctly.
bool Samurai::IO::Net::DNS::Validator::isValidLabel(const char* buf, size_t len)
{
	if (!buf || !len) return false;

	for (size_t n = 0; n < len; n++)
		if (!(isalnum(buf[n]) || buf[n] == '_' || buf[n] == '-'))
			return false;
	return true;
}


bool Samurai::IO::Net::DNS::Validator::isValidName(const char* buf, size_t len)
{
	if (!buf || !len) return false;

	bool period = true; // cannot start with a period, such as ".www.example.com"
	for (size_t n = 0; n < len; n++) {
		if (buf[n] == '.') {
			if (period) return false; // check for double periods.
			else period = true;
		} else {
			period = false;
		}
		
		if (!(isalnum(buf[n]) || buf[n] == '_' || buf[n] == '-'))
			return false;
	}

	return true;
}


Samurai::IO::Net::DNS::Label::Label(const char* val, uint8_t sz) {
	size = sz;
	if (sz > DNS_LABEL_SIZE) {
		memcpy(name, val, DNS_LABEL_SIZE);
		name[DNS_LABEL_SIZE] = 0;
		size = DNS_LABEL_SIZE;
	} else {
		memcpy(name, val, sz);
		name[sz] = 0;
	}
}

Samurai::IO::Net::DNS::Label::Label(const Samurai::IO::Net::DNS::Label& copy) {
	memcpy(name, copy.name, DNS_LABEL_SIZE);
	size = copy.size;
}

Samurai::IO::Net::DNS::Label::Label(Samurai::IO::Net::DNS::Label* copy) {
	memcpy(name, copy->name, DNS_LABEL_SIZE);
	size = copy->size;
}


Samurai::IO::Net::DNS::Label::~Label()
{
	
}

bool Samurai::IO::Net::DNS::Label::isValid()
{
	if (size == 0 || size > DNS_LABEL_SIZE) return false;
	return Samurai::IO::Net::DNS::Validator::isValidLabel((const char*) name, (size_t) size);
}


const char* Samurai::IO::Net::DNS::Label::getName() const
{
	return (const char*) name;
}


uint8_t Samurai::IO::Net::DNS::Label::getSize() const
{
	return size;
}


bool Samurai::IO::Net::DNS::Label::operator==(const Samurai::IO::Net::DNS::Label& label)
{
	if (&label == this) return true;
	if (label.size != size) return false;
	return strncasecmp(label.name, name, size) == 0;
}

bool Samurai::IO::Net::DNS::Label::operator!=(const Samurai::IO::Net::DNS::Label& label)
{
	if (&label == this) return false;
	if (label.size != size) return true;
	return strncasecmp(label.name, name, size) != 0;
}

Samurai::IO::Net::DNS::Name::Name()
{
	name[0] = 0;
	size = 0;
	offset = 0;
}

Samurai::IO::Net::DNS::Name::Name(const char* hostname)
{
	size = strlen(hostname);
	offset = 0;
	if (size > DNS_NAME_SIZE) {
		memcpy(name, hostname, DNS_NAME_SIZE);
		name[DNS_NAME_SIZE] = 0;
		size = DNS_NAME_SIZE;
	} else {
		memcpy(name, hostname, size);
		name[size] = 0;
	}
}

Samurai::IO::Net::DNS::Name::~Name()
{
	clear();
}

Samurai::IO::Net::DNS::Name::Name(const Name& copy) {
	memcpy(name, copy.name, DNS_NAME_SIZE);
	size = copy.size;
	offset = copy.offset;
	for (std::vector<Label*>::iterator it = copy.parts.begin(); it != copy.parts.end(); it++) {
		parts.push_back(new Label((*it)));
	}
}


int Samurai::IO::Net::DNS::Name::split() {
	size_t len = size;
	char* last = name;
	for (size_t n = 0; n < len; n++) {
		if (name[n] == '.') {
			name[n] = 0;
			addPart(new Label((const char*) last, (uint8_t) strlen(last)));
			last = &name[n+1];
		}
	}

	if (strlen(last))
		addPart(new Label((const char*) last, (uint8_t) strlen(last)));

	return countParts();
}


bool Samurai::IO::Net::DNS::Name::isValid() {
	if (countParts() == 0) return false;
	return Samurai::IO::Net::DNS::Validator::isValidName((const char*) name, size);
}


bool Samurai::IO::Net::DNS::Name::join() {
	size_t len = strlen(name);
	(void)len;
	return false;
}

uint8_t Samurai::IO::Net::DNS::Name::countParts() const {
	return parts.size();
}

void Samurai::IO::Net::DNS::Name::addPart(Label* label) {
	// printf("Adding part: '%s'\n", label->getName());
	parts.push_back(label);
}


char* Samurai::IO::Net::DNS::Name::toString() {
	//if (!strlen(name)) {
	name[0] = 0;
	for (std::vector<Label*>::iterator it = parts.begin(); it != parts.end(); it++) {
		strcat(name, (*it)->getName());
		strcat(name, ".");
	}
	// }
	return name;
}

void Samurai::IO::Net::DNS::Name::clear() {
	while (parts.size()) {
		Label* label = (*parts.begin());
		parts.erase(parts.begin());
		delete label;
	}

	size = 0;
	offset = 0;
	name[0] = 0;
}

bool Samurai::IO::Net::DNS::Name::operator==(const Samurai::IO::Net::DNS::Name& name)
{
	if (this == &name) return true;
	if (name.parts.size() != parts.size()) return false;
	
	for (size_t n = 0; n < parts.size(); n++) {
		if (*parts[n] != *name.parts[n]) return false;
	}
	return true;
}

bool Samurai::IO::Net::DNS::Name::operator!=(const Samurai::IO::Net::DNS::Name& name)
{
	if (this == &name) return false;
	if (name.parts.size() != parts.size()) return true;
	
	for (size_t n = 0; n < parts.size(); n++) {
		if (*parts[n] == *name.parts[n]) return false;
	}
	return true;
}


