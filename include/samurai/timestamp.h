/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SYSTEM_TIMESTAMP_H
#define HAVE_SYSTEM_TIMESTAMP_H

#include <time.h>
#include <string>

namespace Samurai {

/**
 * This class represents a timestamp in seconds since the Epoch.
 * (00:00:00 UTC, January 1, 1970), measured in seconds.
 */
class TimeStamp {
	public:
		TimeStamp();
		TimeStamp(time_t);
		TimeStamp(const TimeStamp&);
		~TimeStamp();
		
		/**
		 * Returns the internal timestamp value.
		 */
		time_t getInternalData() const;

		/* Resets or updates the timestamp to the current clock */
		void reset();
		
		/**
		 * Returns a printable timestamp
		 */
		/**
		 * Render the timestamp, with strftime() syntax when a format is given
		 * and ctime()'s default otherwise.
		 *
		 * Returns the string rather than a pointer into a shared buffer: the
		 * previous form wrote into a function-local static (and ctime()'s own),
		 * so two calls in one expression both yielded the same text.
		 */
		std::string getTime(const char* format = nullptr) const;
		
		
		/**
		 * Returns the number of seconds elapsed between the two timestamps.
		 */
		time_t elapsed(const TimeStamp&);
		
		/**
		 * Returns the number of seconds since the TimeStamp was created.
		 */
		time_t elapsed();
		
		void operator=(const TimeStamp&);
		bool operator<(const TimeStamp&);
		bool operator>(const TimeStamp&);
		bool operator==(const TimeStamp&);
		bool operator!=(const TimeStamp&);
		bool operator<=(const TimeStamp&);
		bool operator>=(const TimeStamp&);
		
		TimeStamp operator+(time_t seconds);
		TimeStamp operator-(time_t seconds);
		
	protected:
		time_t data;
};

}

#endif // HAVE_SYSTEM_TIMESTAMP_H
