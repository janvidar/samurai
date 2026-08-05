/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <time.h>
#include <samurai/timestamp.h>

Samurai::TimeStamp::TimeStamp() {
	data = time(nullptr);
}

Samurai::TimeStamp::TimeStamp(time_t t) {
	data = t;
}

Samurai::TimeStamp::TimeStamp(const Samurai::TimeStamp& copy) {
	data = copy.data;
}

Samurai::TimeStamp::~TimeStamp() {

}

/*
time_t Samurai::TimeStamp::getTime() {
	return time(0);
}
*/

void Samurai::TimeStamp::reset() {
	data = time(nullptr);
}

std::string Samurai::TimeStamp::getTime(const char* format) const {
	char buf[64] = { 0, };

	/* strftime() needs a terminated format string, so this one stays a
	   const char* rather than becoming a string_view. */
	if (!format)
		format = "%a %b %e %H:%M:%S %Y";

	const size_t written = strftime(buf, sizeof(buf), format, localtime(&data));
	return std::string(buf, written);
}

time_t Samurai::TimeStamp::getInternalData() const {
	return data;
}

time_t Samurai::TimeStamp::elapsed(const Samurai::TimeStamp& later) {
	return later.data - data;
}

time_t Samurai::TimeStamp::elapsed() {
	return time(nullptr) - data;
}

void Samurai::TimeStamp::operator=(const Samurai::TimeStamp& copy) {
	data = copy.data;
}

bool Samurai::TimeStamp::operator<(const Samurai::TimeStamp& copy) {
	return data < copy.data;
}

bool Samurai::TimeStamp::operator>(const Samurai::TimeStamp& copy) {
	return data > copy.data;
}

bool Samurai::TimeStamp::operator==(const Samurai::TimeStamp& copy) {
	return data == copy.data;
}

bool Samurai::TimeStamp::operator!=(const Samurai::TimeStamp& copy) {
	return data != copy.data;
}

bool Samurai::TimeStamp::operator<=(const Samurai::TimeStamp& copy) {
	return data <= copy.data;
}

bool Samurai::TimeStamp::operator>=(const Samurai::TimeStamp& copy) {
	return data >= copy.data;
}

Samurai::TimeStamp Samurai::TimeStamp::operator+(time_t seconds) {
	TimeStamp temp(data+seconds);
	return temp;
}

Samurai::TimeStamp Samurai::TimeStamp::operator-(time_t seconds) {
	TimeStamp temp(data-seconds);
	return temp;
}

