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

#include "scheduler.h"

void insertTokenInBlock(const Token & token, vector<Block> & output)
{
	if(output.empty())
	{
		output.emplace_back(token);
	}
	else
	{
		Block & block = output.back();

		if(block.blockID == token.finalAddress)
			block.insertToken(token);
		else
		{
			block.compileBlock();
			output.emplace_back(token);
		}
	}
}

bool buildBlockVector(const vector<BSDiffMoves> & input, vector<Block> & output)
{
	vector<Token> tokens;

	//Divide the input per origin/destination page segmentation
	for(auto & item : input)
		Token::insertToken(item, tokens);

	//Sort the tokens so they are sorted based on their destination
	sort(tokens.begin(), tokens.end(),
		[](const Token & a, const Token & b)
		{
			return a.finalAddress < b.finalAddress;
		});

	for(auto & item : tokens)
		insertTokenInBlock(item, output);

	if(!output.empty())
		output.back().compileBlock();

	Block::crossRefsBlocks(output);

	return !output.empty();
}

void Block::compileBlock()
{
	if(!blocksWithDataForCurrent.empty())
		return;

	unordered_map<BlockID, BlockLink> parentsBlocks;
	parentsBlocks.reserve(data.size());

	for(const auto & token : data)
	{
		const BlockID & currentBlockID = token.origin;

		if(currentBlockID == blockID)
			blockNeedSwap = true;
		else
		{
			BlockLink link(currentBlockID, token.length);

			auto iterator = parentsBlocks.find(currentBlockID);
			if(iterator != parentsBlocks.end())
			{
				iterator->second.volume += link.volume;
			}
			else
			{
				parentsBlocks.insert({currentBlockID, link});
			}
		}
	}

	if(!parentsBlocks.empty())
	{
		blocksWithDataForCurrent.reserve(parentsBlocks.size());

		for(const auto & item : parentsBlocks)
			blocksWithDataForCurrent.insert(blocksWithDataForCurrent.end(), item.second);

		sort(blocksWithDataForCurrent.begin(), blocksWithDataForCurrent.end());
	}
}

void Block::crossRefsBlocks(vector<Block> & blocks)
{
	unordered_map<BlockID, vector<BlockLink>> crossRef;
	crossRef.reserve(blocks.size());

	for(const auto & block : blocks)
	{
		for(const auto & ref : block.blocksWithDataForCurrent)
			crossRef[ref.block].emplace_back(BlockLink(block.blockID, ref.volume));
	}

	for(auto & block : blocks)
	{
		block.blocksRequestingData = crossRef[block.blockID];
	}
}
