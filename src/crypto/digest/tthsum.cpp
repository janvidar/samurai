/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/crypto/digest/tiger.h>
#include <samurai/crypto/digest/tthsum.h>
#include <samurai/util/base32.h>
#include <stdlib.h>

int MerkleNodeCompare(const void* a, const void* /*b*/)
{
	return a ? 1 : -1;
}








/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */


