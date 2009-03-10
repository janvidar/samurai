/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <time.h>
#include <samurai/timestamp.h>

Samurai::TimeStamp::TimeStamp() {
	data = time(0);
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
	data = time(0);
}

const char* Samurai::TimeStamp::getTime(const char* format) {
	if (!format) {
		return ctime(&data);
	} else {
		static char timestamp_buf[64] = { 0, };
		strftime(timestamp_buf, 64, format, localtime(&data));
		return timestamp_buf;
	}
}

time_t Samurai::TimeStamp::getInternalData() const {
	return data;
}

time_t Samurai::TimeStamp::elapsed(const Samurai::TimeStamp& later) {
	return later.data - data;
}

time_t Samurai::TimeStamp::elapsed() {
	return time(0) - data;
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

