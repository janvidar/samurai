/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/crypto/digest/tiger.h>
#include <samurai/crypto/digest/merkletree.h>
#include <samurai/util/base32.h>

static uint8_t leaf_hash_prefix[1] = { 0x00 };
static uint8_t node_hash_prefix[1] = { 0x01 };


#if 0
/* UNUSED CODE */
static uint64_t calc_size_depth(size_t depth)
{
	uint64_t size = 2;
	for (size_t n = 1; n < depth; n++)
		size *= 2;
	return --size;
}

static uint64_t calcMinimumBlockSize(size_t& block_size, size_t depth, uint64_t file_size)
{
	size_t max_leaves = calcMaxNumLeaves(depth);
	while ((file_size / block_size) > max_leaves)
		block_size <<= 2;
	return block_size;
}
#endif


static size_t calcMaxNumLeaves(size_t depth)
{
	if (depth < 1) return 1;
	size_t level = 1;
	for (size_t n = 1; n < depth; n++)
	{
		level *= 2;
	}
	return level;
}



Samurai::Crypto::Digest::MerkleNode::MerkleNode(Samurai::Crypto::Digest::HashValue* value) : Samurai::Crypto::Digest::HashValue(value)
{

}


Samurai::Crypto::Digest::MerkleNode::~MerkleNode()
{

}

Samurai::Crypto::Digest::MerkleWorkStack::MerkleWorkStack(size_t capacity_) : capacity(capacity_)
{
	nodes = new Samurai::Crypto::Digest::MerkleNode*[capacity];
	clear();
}

Samurai::Crypto::Digest::MerkleWorkStack::~MerkleWorkStack()
{
	delete[] nodes;
}

void Samurai::Crypto::Digest::MerkleWorkStack::grow()
{
	size_t sz = capacity * 2;
	MerkleNode** new_nodes = new MerkleNode*[sz];
	size_t i = 0;
	for (; i < capacity; i++)
		new_nodes[i] = nodes[i];
	for (; i < sz; i++)
		new_nodes[i] = 0;
	
	delete[] nodes;
	nodes = new_nodes;	
	capacity = sz;
}


void Samurai::Crypto::Digest::MerkleWorkStack::add(MerkleNode* node)
{
	if (isFull()) grow();
	nodes[pos++] = node;	
	size++;
}


void Samurai::Crypto::Digest::MerkleWorkStack::removeLast()
{
	nodes[pos-1] = 0;
	pos--;
}


Samurai::Crypto::Digest::MerkleNode* Samurai::Crypto::Digest::MerkleWorkStack::get(size_t apos)
{
	return nodes[apos];
}


Samurai::Crypto::Digest::MerkleNode* Samurai::Crypto::Digest::MerkleWorkStack::getLast()
{
	return get(pos-1);
}


Samurai::Crypto::Digest::MerkleNode* Samurai::Crypto::Digest::MerkleWorkStack::getFirst()
{
	return get(pos-1);
}


void Samurai::Crypto::Digest::MerkleWorkStack::set(Samurai::Crypto::Digest::MerkleNode* node, size_t npos)
{
	nodes[npos] = node;
}


void Samurai::Crypto::Digest::MerkleWorkStack::clear()
{
	size = 0;
	pos = 0;
	for (size_t i = 0; i < capacity; i++)
		nodes[i] = 0;	
}


void Samurai::Crypto::Digest::MerkleWorkStack::setPosition(size_t npos)
{
	pos = npos;
	for (size_t i = pos; i < capacity; i++)
		nodes[i] = 0;
}


bool Samurai::Crypto::Digest::MerkleWorkStack::isFull() const
{
	return pos == capacity;
}



Samurai::Crypto::Digest::MerkleTree::MerkleTree(Samurai::Crypto::Digest::Hash* hasher, size_t block_size, size_t max_levels) : Samurai::Crypto::Digest::Hash(hasher->size(), block_size, false, false, 0)
{
	m_hasher = hasher;
	m_hasher->reset();
	
	m_max_levels = max_levels;
	m_max_leaves = calcMaxNumLeaves(m_max_levels);
	m_blocks_per_leaf = 1;
	m_count = 0;
	m_nodes = new MerkleWorkStack(m_max_leaves);
	m_work  = new MerkleWorkStack(m_max_leaves);
}

size_t Samurai::Crypto::Digest::MerkleTree::countLeaves()
{
	return m_nodes->getPosition();
}

/**
 * This will reset everything, and set a new set of leaves.
 */
void Samurai::Crypto::Digest::MerkleTree::setLeavesLTR(Samurai::IO::Buffer& buffer, size_t leaves, uint64_t file_size)
{
	size_t leaf_size = size();
	assert(leaves * leaf_size == buffer.size());
	reset();
	
	Samurai::Crypto::Digest::HashValue value(leaf_size);
	
	uint8_t* buf = new uint8_t[size()];
	for (size_t n = 0; n < leaves; n++)
	{
		buffer.pop((char*) buf, n*leaf_size, leaf_size);
		value.setData(buf);
		
		MerkleNode* node = new MerkleNode(&value);
		m_nodes->add(node);
	}
	delete[] buf;
	m_count = file_size;
}

/**
 * This will reset everything, and set a new set of leaves.
 */
void Samurai::Crypto::Digest::MerkleTree::setLeavesRTL(Samurai::IO::Buffer& buffer, size_t leaves, uint64_t file_size)
{
	size_t leaf_size = size();
	assert(leaves * leaf_size == buffer.size());
	reset();
	
	Samurai::Crypto::Digest::HashValue value(leaf_size);
	
	uint8_t* buf = new uint8_t[size()];
	for (size_t n = leaves; n > 0; n--)
	{
		buffer.pop((char*) buf, ((n-1)*leaf_size), leaf_size);
		value.setData(buf);
		
		MerkleNode* node = new MerkleNode(&value);
		m_nodes->add(node);
	}
	delete[] buf;
	m_count = file_size;
}



/**
 * Copy the leaf node data to this buffer.
 */
void Samurai::Crypto::Digest::MerkleTree::copyLeavesLTR(Samurai::IO::Buffer& buffer)
{
	MerkleNode* node = 0;
	size_t leaf_size = size();
	for (size_t n = 0; n < m_nodes->getPosition(); n++)
	{
		node = m_nodes->get(n);
		buffer.append((const char*) node->getData(), leaf_size);
	}
}

/**
 * Copy the leaf node data to this buffer.
 */
void Samurai::Crypto::Digest::MerkleTree::copyLeavesRTL(Samurai::IO::Buffer& buffer)
{
	MerkleNode* node = 0;
	size_t leaf_size = size();
	for (size_t n = m_nodes->getPosition(); n > 0; n--)
	{
		node = m_nodes->get(n-1);
		buffer.append((const char*) node->getData(), leaf_size);
	}
}



size_t Samurai::Crypto::Digest::MerkleTree::maxLeaves()
{
	return m_nodes->getCapacity();
}


size_t Samurai::Crypto::Digest::MerkleTree::getLevels()
{
	size_t depth = m_blocks_per_leaf;
	if (depth > m_max_levels)
		depth = m_max_levels;
	return depth;
}


Samurai::Crypto::Digest::MerkleTree::~MerkleTree()
{
	
}


void Samurai::Crypto::Digest::MerkleTree::reset()
{
	m_hasher->reset();
	m_blocks_per_leaf = 1;
	m_count = 0;
	delete m_nodes;
	delete m_work;
	m_nodes = new MerkleWorkStack(m_max_leaves);
	m_work  = new MerkleWorkStack(m_max_leaves);
	m_finalized = false;
}

void Samurai::Crypto::Digest::MerkleTree::finalize()
{
	if (m_finalized) return;

	// Hash any remaining incomplete blocks
	if (m_current_block_index > 0 || m_count == 0)
	{
		hash(m_current_block, m_current_block_index);
	}
	
	// Promote all remaining nodes as a final leaf node.
	// Empty the work stack.
	if (m_work->getSize())
	{
		compact_all(m_work);
		m_nodes->add(m_work->getFirst());
		m_work->clear();
	}

//	QDBG("Samurai::Crypto::Digest::MerkleTree::finalize: nodes=%zu/%zu", m_nodes->getPosition(), m_nodes->getSize());

	// Save the leaf nodes in the work stack (which is empty)
	for (size_t n = 0; n < m_nodes->getPosition(); n++)
		m_work->add(new MerkleNode(m_nodes->get(n)));

	// Reduce to one node (root node).
	compact_all(m_nodes);
	
	MerkleNode* root = m_nodes->getFirst();
	set_finalized_value(root->getData());
	delete root;
	m_nodes->clear();
	
	// Now, empty the work stack, and move the final leaf nodes back.
	for (size_t n = 0; n < m_work->getPosition(); n++)
		m_nodes->add(m_work->get(n));
	
	m_work->clear();
	
	// Adding more data will not do anything, unless the class is reset().
	m_finalized = true;
}


Samurai::Crypto::Digest::HashValue* Samurai::Crypto::Digest::MerkleTree::digest()
{
	finalize();
	return &m_finalized_value;
}


Samurai::Crypto::Digest::MerkleNode* Samurai::Crypto::Digest::MerkleTree::combine(Samurai::Crypto::Digest::MerkleNode* a, Samurai::Crypto::Digest::MerkleNode* b)
{
	m_hasher->reset();
	m_hasher->update(node_hash_prefix, 1);
	m_hasher->update(a->getData(), m_hasher->size());
	m_hasher->update(b->getData(), m_hasher->size());
	Samurai::Crypto::Digest::HashValue* value = m_hasher->digest();
	a->setData(value->getData());
	return a;
}


void Samurai::Crypto::Digest::MerkleTree::compact(MerkleWorkStack* stack)
{
	MerkleNode* a;
	MerkleNode* b;
	size_t j = 0;
	
	for (size_t i = 0; i < stack->getPosition(); i += 2)
	{
		a = stack->get(i);
		b = stack->get(i+1);
		a = combine(a, b);
		delete b;
		stack->set(a, j++);
	}
	stack->setPosition(j);
}


void Samurai::Crypto::Digest::MerkleTree::compact_all(MerkleWorkStack* stack)
{
	while (stack->getPosition() > 1)
	{
		MerkleNode* spare = 0;
		if (stack->getPosition() % 2 == 1)
		{
			spare = stack->getLast();
			stack->removeLast();
		}
		
		compact(stack);
		
		if (spare) stack->add(spare);
	}
}


void Samurai::Crypto::Digest::MerkleTree::hash(uint8_t* data, size_t length)
{
	if (m_finalized) return;

	if (m_nodes->isFull())
	{
		// single compact
		compact(m_nodes);
		// QDBG("... leaf nodes=%zu (compacted)", m_nodes->getPosition());
		m_blocks_per_leaf <<= 1;
	}
	
	m_hasher->reset();
	m_hasher->update(leaf_hash_prefix, 1);
	m_hasher->update(data, length);
	Samurai::Crypto::Digest::HashValue* value = m_hasher->digest();
	MerkleNode* node = new MerkleNode(value);
	
	m_work->add(node);
	if (m_work->getSize() == m_blocks_per_leaf)
	{
		compact_all(m_work);
		m_nodes->add(m_work->getFirst());
		// QDBG("... leaf nodes=%zu", m_nodes->getPosition());
		
		m_work->clear();
	}
	
	m_count++;
}

