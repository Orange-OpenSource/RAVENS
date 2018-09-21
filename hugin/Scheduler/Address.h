/*
 * Copyright (C) 2018 Orange
 *
 * This software is distributed under the terms and conditions of the 'BSD-3-Clause-Clear'
 * license which can be found in the file 'LICENSE.txt' in this package distribution
 * or at 'https://spdx.org/licenses/BSD-3-Clause-Clear.html'.
 */

/**
 * @author Emile-Hugo Spir
 */

#ifndef HERMES_ADDRESS_H
#define HERMES_ADDRESS_H

struct Address;

class BlockID
{
public:
	size_t value;

	BlockID(const BlockID & block) : value(block.value) {}
	BlockID(size_t newValue) : value(newValue & BLOCK_MASK) {}

	BlockID operator+(const size_t & a) const	{	return value + (a << BLOCK_SIZE_BIT);	}
	bool operator==(const BlockID & b) const	{	return value == b.value;	}
	bool operator!=(const BlockID & b) const	{	return value != b.value;	}
	bool operator>=(const BlockID & b) const	{	return value >= b.value;	}
	bool operator <(const BlockID & b) const	{	return value <  b.value;	}
	bool operator >(const BlockID & b) const	{	return value >  b.value;	}
};

namespace std
{
	template<>
	struct hash<BlockID>
	{
		size_t operator()(const BlockID & obj) const
		{
			return obj.value;
		}
	};
}

struct Address
{
	size_t value;

	Address(const BlockID & block, const size_t & offset) : value(block.value + (offset & BLOCK_OFFSET_MASK)) {}
	explicit Address(const size_t & newValue) : value(newValue) {}

	operator BlockID() const {	return getBlock();	}
	explicit operator size_t() const {	return getOffset();	}

	Address operator+(size_t b) const 	{	return Address(value + b); }
	Address operator+(int64_t b) const	{	return Address(value + b); }

	void operator+=(size_t b)
	{
		value += b;
	}

	bool operator <(const Address & b) const	{	return value <  b.value;		}
	bool operator >(const Address & b) const	{	return value >  b.value;		}
	bool operator<=(const Address & b) const	{	return value <= b.value;		}
	bool operator>=(const Address & b) const	{	return value >= b.value;		}
	bool operator==(const Address & b) const	{	return value == b.value;	}
	bool operator==(const BlockID & b) const	{	return b == value;	}
	bool operator!=(const BlockID & b) const	{	return b != value;	}
	bool operator<(const BlockID & b) const	{	return b > value;	}
	bool operator>(const BlockID & b) const	{	return b < value;	}
	void operator-=(const size_t b)				{	value -= b;	}

	BlockID getBlock() const { return value & BLOCK_MASK;	}
	size_t getOffset() const { return value & BLOCK_OFFSET_MASK;	}
	size_t getAddress() const { return value; }
};

#endif //HERMES_ADDRESS_H
