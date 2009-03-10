/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/util/refcount.h>
#include <stdlib.h>

Samurai::Util::RefCounter::RefCounter() { p_reference = 0; }
		
Samurai::Util::RefCounter::~RefCounter() {
	if (!canDelete()) {
		abort();
		QERR("Deleting object while references still occur");
	}
	p_reference = 0xdeadbeef;
}
	
void Samurai::Util::RefCounter::incRef() {
	p_reference++;
}

void Samurai::Util::RefCounter::decRef() {
	p_reference--;
}

bool Samurai::Util::RefCounter::canDelete() {
	return !p_reference;
}

size_t Samurai::Util::RefCounter::getRef() const {
	return p_reference;
}
