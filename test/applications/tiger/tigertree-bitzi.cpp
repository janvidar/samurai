/*
 * Copyright (C) 2001-2006 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

/* (PD) 2001 The Bitzi Corporation
 * Please see file COPYING or http://bitzi.com/publicdomain 
 * for more info.
 *
 * tigertree.c - Implementation of the TigerTree algorithm
 *
 * NOTE: The TigerTree hash value cannot be calculated using a
 * constant amount of memory; rather, the memory required grows
 * with the size of input. (Roughly, one more interim value must
 * be remembered for each doubling of the input size.) The
 * default TT_CONTEXT struct size reserves enough memory for
 * input up to 2^64 in length
 *
 * Requires the tiger() function as defined in the reference
 * implementation provided by the creators of the Tiger
 * algorithm. See
 *
 *    http://www.cs.technion.ac.il/~biham/Reports/Tiger/
 *
 * $Id: tigertree.cpp,v 1.2 2004/08/12 12:44:35 mathen Exp $
 *
 */

#include <string.h>
#include "tigertree-bitzi.h"
#include <samurai/crypto/digest/tiger.h>

// #define DUMP_MERKLE_TREE_DEBUG
#ifndef DUMP_MERKLE_TREE_DEBUG
#undef QDBG
#define QDBG(format, ...) do { } while(0)
#endif


namespace Bitzi {

/*
 * NOTE: the scratch areas are deliberately unaligned - tt_init() sets
 * ctx->block to ctx->leaf + 1, an odd address - so this takes uint8_t* and must
 * not be reached through a wider pointer type: that is undefined behaviour and
 * faults outright on targets requiring natural alignment.
 */
static void tiger(const uint8_t* str, size_t length, uint8_t* res)
{
	Samurai::Crypto::Digest::Tiger tiger;
	tiger.update(str, length);
	const Samurai::Crypto::Digest::HashValue& value = tiger.digest();
	memcpy(res, value.getData(), value.size());
}


/* Initialize the tigertree context */
void tt_init(TT_CONTEXT *ctx)
{
  ctx->count = 0;
  ctx->leaf[0] = 0; // flag for leaf  calculation -- never changed
  ctx->node[0] = 1; // flag for inner node calculation -- never changed
  ctx->block = ctx->leaf + 1 ; // working area for blocks
  ctx->index = 0;   // partial block pointer/block length
  ctx->top = ctx->nodes;
}

static void tt_compose(TT_CONTEXT *ctx) {
  uint8_t *node = ctx->top - NODESIZE;
  memmove((ctx->node)+1,node,NODESIZE); // copy to scratch area
  tiger(ctx->node, (size_t)(NODESIZE+1), ctx->top); // combine two nodes
  memmove(node,ctx->top,TIGERSIZE);           // move up result
  ctx->top -= TIGERSIZE;                      // update top ptr
  
  QDBG("Combine");
}

static void tt_block(TT_CONTEXT *ctx)
{
	QDBG("Block %d", (int) ctx->count);
	if (ctx->index > 0 && ctx->index != BLOCKSIZE)
	{
		QDBG("Partial index %d/%d", (int) ctx->index, (int) BLOCKSIZE);
	}
	
	uint64_t b;
	
	tiger(ctx->leaf, ctx->index + 1, ctx->top);
	ctx->top += TIGERSIZE;
	++ctx->count;
	b = ctx->count;
	while(b == ((b >> 1)<<1)) { // while evenly divisible by 2...
		tt_compose(ctx);
		b = b >> 1;
	}
}

void tt_update(TT_CONTEXT *ctx, std::span<const uint8_t> input)
{
  const uint8_t* buffer = input.data();
  size_t len = input.size();

  if (ctx->index)
  { /* Try to fill partial block */
	  size_t left = BLOCKSIZE - ctx->index;
	  if (len < left)
		{
		memmove(ctx->block + ctx->index, buffer, len);
		ctx->index += len;
		return; /* Finished */
		}
	  else
		{
		memmove(ctx->block + ctx->index, buffer, left);
		ctx->index = BLOCKSIZE;
		tt_block(ctx);
		buffer += left;
		len -= left;
		}
  }

  while (len >= BLOCKSIZE)
	{
	memmove(ctx->block, buffer, BLOCKSIZE);
	ctx->index = BLOCKSIZE;
	tt_block(ctx);
	buffer += BLOCKSIZE;
	len -= BLOCKSIZE;
	}
  if ((ctx->index = len))     /* This assignment is intended */
	{
	/* Buffer leftovers */
	memmove(ctx->block, buffer, len);
	}
}

// no need to call this directly; tt_digest calls it for you
static void tt_final(TT_CONTEXT *ctx)
{
	QDBG("Finalizing (%d blocks so far)", (int) ctx->count);
	
  // do last partial block, unless index is 1 (empty leaf)
  // AND we're past the first block
  if((ctx->index>0)||(ctx->top==ctx->nodes))
    tt_block(ctx);
}

void tt_digest(TT_CONTEXT *ctx, std::span<uint8_t, TIGERSIZE> s)
{
	QDBG("Digesting 1");
	tt_final(ctx);
	QDBG("Digesting 2");
	while( (ctx->top-TIGERSIZE) > ctx->nodes ) {
		tt_compose(ctx);
	}
	memmove(s.data(), ctx->nodes, TIGERSIZE);
}

/*
 * NOTE: 'block' and 'top' are interior pointers into the context's own arrays,
 * so a copy has to rebuild them as offsets into the destination rather than
 * assigning the source's.
 */
void tt_copy(TT_CONTEXT *dest, TT_CONTEXT *src)
{
  if (!dest || !src) return;

  dest->count = src->count;
  dest->index = src->index;

  memcpy(dest->leaf, src->leaf, sizeof(dest->leaf));
  memcpy(dest->node, src->node, sizeof(dest->node));
  memcpy(dest->nodes, src->nodes, sizeof(dest->nodes));

  dest->block = dest->leaf + 1;
  dest->top   = dest->nodes + (src->top - src->nodes);
}

}
