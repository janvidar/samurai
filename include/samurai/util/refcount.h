/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_QUICKDC_REFCOUNTER_H
#define HAVE_QUICKDC_REFCOUNTER_H

#include <samurai/samurai.h>
#include <samurai/util/refcount.h>

namespace Samurai {
namespace Util {

class RefCounter {
	public:
		RefCounter();
		
		virtual ~RefCounter();
		
		void incRef();
		void decRef();
		bool canDelete();
		size_t getRef() const;
	private:
		size_t p_reference;
};

}
}
#endif // HAVE_QUICKDC_REFCOUNTER_H
