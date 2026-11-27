/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <time.h>
#include <string>
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

	/* %s, seconds since the epoch, is a GNU extension the Windows C runtime
	   aborts on, so it is expanded here on every platform. */
	std::string expanded;
	for (const char* c = format; *c; c++)
	{
		if (c[0] == '%' && c[1] == 's') { expanded += std::to_string((long long) data); c++; }
		else if (c[0] == '%' && c[1]) { expanded += c[0]; expanded += c[1]; c++; }
		else expanded += *c;
	}

	struct tm local = {};
#ifdef _WIN32
	localtime_s(&local, &data);
#else
	localtime_r(&data, &local);
#endif
	const size_t written = strftime(buf, sizeof(buf), expanded.c_str(), &local);
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

