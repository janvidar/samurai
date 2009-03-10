/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_EXCEPTION_H
#define HAVE_SAMURAI_EXCEPTION_H

namespace Samurai {
	class Exception {
		public:
			Exception(const char* exception) : name(exception) { }
			const char* getName() { return name; }
		
		protected:
			const char* name;
	};
}

#endif // HAVE_SAMURAI_EXCEPTION_H
