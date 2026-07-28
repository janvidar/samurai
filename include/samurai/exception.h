/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_EXCEPTION_H
#define HAVE_SAMURAI_EXCEPTION_H

#include <exception>

namespace Samurai {
	/**
	 * Base for the library's exception types.
	 *
	 * Derives from std::exception so a generic handler can catch it, and has a
	 * virtual destructor because it is meant to be derived from and thrown by
	 * reference.
	 */
	class Exception : public std::exception {
		public:
			explicit Exception(const char* exception) noexcept : name(exception) { }
			~Exception() override = default;

			const char* getName() const noexcept { return name; }
			const char* what() const noexcept override { return name; }

		protected:
			const char* name;
	};
}

#endif // HAVE_SAMURAI_EXCEPTION_H
