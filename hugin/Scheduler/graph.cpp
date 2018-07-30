//
// Created by Emile-Hugo Spir on 3/14/18.
//

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

	for(auto & block : output)
		block.crossRefsBlocks(output);

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
	for(const auto & block : blocks)
	{
		//We ignore ourselves
		if(block.blockID == blockID)
			continue;

		//BlockIDs are sorted
		auto iter = lower_bound(block.blocksWithDataForCurrent.begin(), block.blocksWithDataForCurrent.end(), blockID, [](const BlockLink & a, const BlockID & b) { return a.block < b; });

		if(iter != block.blocksWithDataForCurrent.end() && iter->block == blockID)
			blocksRequestingData.emplace_back(BlockLink(block.blockID, iter->volume));
	}
}
