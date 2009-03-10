/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_QUICKDC_MERKLE_TREE_H
#define HAVE_QUICKDC_MERKLE_TREE_H

#include <samurai/samurai.h>
#include <samurai/io/buffer.h>
#include <samurai/crypto/digest/tigertree.h>

namespace Samurai {
namespace Crypto {
namespace Digest {
/**

A Merkle tree is built by sending in data which is split into chunks 1024 bytes each (default chunk size).

       IH(A+B)
     /        \
A=LH(S1) + B=LH(S2)

So, sending in data, when the first 1024 bytes are reached you will have block S1, which 
should be hashed as a leaf hash.

A=LH(S1)

Continuing onwards, sending another 1024 bytes to the tree will create S2, also to be leaf hashed.
Now, since, these blocks are complete, they should be combined/joined to a internal "top" or "root" hash
to create a tree. They are combined using the internal hash method.

       IH(A+B)
      /       \
A=LH(S1)   B=LH(S2)

Now continuing adding bytes we will have another node S3, and the tree will look something like this:
'root' is IH( IH(A+B) + C).

                root
              /     \
       IH(A+B)       C
      /       \       \
A=LH(S1)   B=LH(S2)  C=LH(S3)

Adding another block of data S4, will continue creating the tree.

                  root
             /           \
            /             \
      IH(A+B)             IH(C+D)
    /        \           /       \
A=LH(S1)  B=LH(S2)   C=LH(S3)  D=LH(S4)


Adding another block of data S5, will expand the tree further;

                            new_root
                        /             \
                       /               \
                old_root                E
             /           \               \
            /             \               \
      IH(A+B)             IH(C+D)          E
    /        \           /       \          \
A=LH(S1)  B=LH(S2)   C=LH(S3)  D=LH(S4)   E=LH(S5)


... And so the story continues.




About tree depth


Different tree depths can be set. The ADC protocol requires a minimum amount of 7 levels kept.
The formula for calculating the size of such a tree is:

where n is the number of levels given. At n=7 this gives 127 nodes in the tree.
An implementation can stack up nodes in an array or list and when reaching such a limit combine
every other node to compact the tree.

At a tree depth of 7 a file of 64KB can be verifified down to its basic chunk size.
A file of 1MB will need 11 levels to do the same, however, this will require a substantial
amount of memory to hold all the information.
Instead one can increase the block size to reduce the number of leaves, so each leaf size will in fact be doubled.

Verifying be performed while transfering, and the branch should be easy to verify directly in the
main tree. A 1MB file will hence be split into transferable chunks of 16KB.
This chunk size is too small for practical purposes, as it adds lag to the transfers if
the client were to send a new request for every 16KB it receives, so a larger minimum chunk size is
adviced (suggest at least 64KB or 128KB, depending on transfer speed).

Some formulas

tree depth: n >= 1.
number of leaf nodes for a tree:      sum(Leafs) = 2^(n-1)
sum of all nodes:                     sum(Leafts+Internal) = (2^n)-1
suggested chunk size for transfer:    file_size / sum(Leafs)

Determining the tree depth when file_size and all leafs are known.

*/

class MerkleNode : public Samurai::Crypto::Digest::HashValue
{
	public:
		MerkleNode(Samurai::Crypto::Digest::HashValue* value);
		~MerkleNode();
		
	private:
		MerkleNode();
};

class MerkleWorkStack
{
	public:
		MerkleWorkStack(size_t capacity);
		~MerkleWorkStack();
	
		uint64_t getSize()   { return size; }
		size_t getPosition() { return pos; }
		size_t getCapacity() { return capacity; }
		bool isFull() const;
		
		void clear();
		
		void add(MerkleNode* node);
		
		void set(MerkleNode* node, size_t position);
		void setPosition(size_t position);
		
		MerkleNode* get(size_t pos);
		MerkleNode* getFirst();
		MerkleNode* getLast();
		
		void removeLast();
		
		void grow();
		
	private:
		MerkleNode** nodes;
		uint64_t size;
		size_t pos;
		size_t capacity;
		
};


class MerkleTree : public Samurai::Crypto::Digest::Hash
{
	public:
		/**
		 * Create a Merkle tree using the given hasher for hashing.
		 */
		MerkleTree(Samurai::Crypto::Digest::Hash* hasher, size_t block_size = 1024, size_t max_levels = 7);
		
		virtual ~MerkleTree();
		
		virtual size_t size() const
		{
			return m_hasher->size();
		}
		
		virtual void reset();

		virtual void finalize();
		
		virtual Samurai::Crypto::Digest::HashValue* digest();
		
		size_t countLeaves();
		size_t maxLeaves();
		
		/**
		 * Copy the leaf node data to this buffer.
		 */
		void copyLeavesLTR(Samurai::IO::Buffer& buffer);
		
		/**
		 * Copy leaves right to left.
		 */
		void copyLeavesRTL(Samurai::IO::Buffer& buffer);
		
		/**
		 * This will reset everything, and set a new set of leaves.
		 */
		void setLeavesLTR(Samurai::IO::Buffer& buffer, size_t leaves, uint64_t file_size);
		/**
		 * This will reset everything, and set a new set of leaves.
		 */
		void setLeavesRTL(Samurai::IO::Buffer& buffer, size_t leaves, uint64_t file_size);
		
		/**
		 * Returns the maximun number of levels
		 * that will be kept in the tree ("tree depth").
		 */
		size_t getMaxLevels() const { return m_max_levels; }
	
		/**
		 * Returns the block size used for a single leaf node.
		 */
		size_t getBlockSize() const { return m_block_size; }
	
		/**
		 * Returns the number of levels (or the tree depth).
		 */
		size_t getLevels();
		
	protected:
		
		void compact(MerkleWorkStack* stack);
		void compact_all(MerkleWorkStack* stack);
		
		/**
		 * Combines two hashes and produces a new node of the internal hash of the two.
		 */
		MerkleNode* combine(MerkleNode* a, MerkleNode* b);

		/**
		 * Internal mehod for hashing leaf data.
		 * length is always block_size, except for the last leaf which may be shorter.
		 */
		virtual void hash(uint8_t* data, size_t length);
		
	private:
		Samurai::Crypto::Digest::Hash* m_hasher;
		MerkleWorkStack* m_nodes;
		MerkleWorkStack* m_work;
		uint64_t m_count;
		size_t   m_max_levels;
		size_t   m_max_leaves;
		size_t   m_leaf_block_size;
		size_t   m_blocks_per_leaf;
};




}
}
}

#endif // HAVE_QUICKDC_MERKLE_TREE_H
