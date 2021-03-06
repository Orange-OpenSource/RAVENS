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

#ifndef RAVENS_BLOCK_H
#define RAVENS_BLOCK_H

struct BlockLink
{
	BlockID block;
	size_t volume;

	BlockLink(const BlockID & blockID) : block(blockID), volume(0) {}
	BlockLink(const BlockID & blockID, const size_t & volume) : block(blockID), volume(volume) {}

	bool operator==(const BlockLink & b) const	{	return block == b.block;	}
	bool operator!=(const BlockLink & b) const	{	return block != b.block;	}
	bool operator<(const BlockLink & b) const	{	return block < b.block;		}
	operator BlockID() const {	return block;	}
};

namespace std
{
	template<>
	struct hash<BlockLink>
	{
		size_t operator()(const BlockLink & obj) const
		{
			return obj.block.value;
		}
	};
}

struct Block
{
	BlockID blockID;
	bool blockNeedSwap;
	bool blockFinished;

	vector<BlockLink> blocksRequestingData;
	vector<BlockLink> blocksWithDataForCurrent;
	vector<Token> data;

	Block(const Token & token) : blockID(token.finalAddress), blockNeedSwap(false), blockFinished(false), data({ token }) {}

	void insertToken(const Token & token)
	{
		data.emplace_back(token);
	}

	void compileBlock();
	static void crossRefsBlocks(vector<Block> & blocks);

	bool operator<(const BlockID & b) const {	return blockID < b;	}
	bool operator>(const BlockID & b) const {	return blockID > b;	}
};

#endif //RAVENS_BLOCK_H
