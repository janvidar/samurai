/*
 * Copyright (C) 2001-2006 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_TIGER_TREE_H
#define HAVE_SAMURAI_TIGER_TREE_H

/* (PD) 2001 The Bitzi Corporation
 * Please see file COPYING or http://bitzi.com/publicdomain 
 * for more info.
 *
 * $Id: tigertree.h,v 1.2 2004/08/10 20:14:23 mathen Exp $
 */
#include <samurai/crypto/digest/tiger.h>

/*
 * NOTE: tt_init, tt_update, tt_digest and tt_copy are the names of the Bitzi
 * reference API that DC++ and the gnutella clients also derive from, so they
 * stay inside the namespace with C++ linkage: unmangled globals would let a
 * consumer that links a second TigerTree implementation bind to either. The
 * sizes are constants for the same reason - this is an installed header, and
 * BLOCKSIZE is not a name to be defining in one.
 */

namespace Samurai {
namespace Crypto {
namespace Digest {

/* tiger hash result size, in bytes */
inline constexpr size_t TIGERSIZE = 24;

/* size of each block independently tiger-hashed, not counting leaf 0x00 prefix */
inline constexpr size_t BLOCKSIZE = 1024;

/* size of input to each non-leaf hash-tree node, not counting node 0x01 prefix */
inline constexpr size_t NODESIZE = TIGERSIZE * 2;

/* default size of interim values stack, in TIGERSIZE
 * blocks. If this overflows (as it will for input
 * longer than 2^64 in size), havoc may ensue. */
inline constexpr size_t STACKSIZE = TIGERSIZE * 56;

typedef struct tt_context {
  uint64_t count;                 /* total blocks processed */
  uint8_t  leaf[BLOCKSIZE+1];     /* leaf in progress */
  uint8_t* block;                 /* leaf data */
  uint8_t  node[NODESIZE+1];      /* node scratch space */
  size_t   index;                 /* index into block */
  uint8_t* top;                   /* top (next empty) stack slot */
  uint8_t  nodes[STACKSIZE];      /* stack of interim node values */
} TT_CONTEXT;

void tt_init(TT_CONTEXT* ctx);
void tt_update(TT_CONTEXT* ctx, std::span<const uint8_t> buffer);
/* 'hash' receives exactly TIGERSIZE bytes. */
void tt_digest(TT_CONTEXT* ctx, std::span<uint8_t, TIGERSIZE> hash);
void tt_copy(TT_CONTEXT* dest, TT_CONTEXT* src);

}
}
}

#endif // HAVE_SAMURAI_TIGER_TREE_H
