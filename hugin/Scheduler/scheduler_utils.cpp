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

#include <algorithm>
#include <unordered_set>
#include <queue>
#include "scheduler.h"

namespace Scheduler
{
	void interpretBlockSort(const Block &block, SchedulerData &commands, bool transactionBundling)
	{
		if(transactionBundling)
			commands.newTransaction();

		commands.insertCommand({LOAD_AND_FLUSH, block.blockID});

		for (const Token &token : block.data)
		{
			if(token.origin == block.blockID)
				commands.insertCommand({COPY, CACHE_BUF + token.origin.getOffset(), token.length, token.finalAddress});
		}

		if(transactionBundling)
			commands.finishTransaction();
	}

	size_t indexOfBlockID(const vector<Block> & block, const BlockID & blockID)
	{
		const auto & matchingBlock = lower_bound(block.cbegin(), block.cend(), blockID);
		return static_cast<size_t>(distance(block.cbegin(), matchingBlock));
	}

	void extractNetwork(const vector<Block> & blocks, const size_t & baseBlockIndex, vector<size_t> & output)
	{
		unordered_set<size_t> networkMembers;
		queue<size_t> potentialMembers({baseBlockIndex});
		const size_t nbBlocks = blocks.size();

		while(!potentialMembers.empty())
		{
			const size_t currentIndex = potentialMembers.front();
			const Block & currentBlock = blocks[currentIndex];

			potentialMembers.pop();

			//Finished blocks are source of data but not actually part of the network
			//We know for sure that any block have data incoming because it survived the first two passes
			if(currentBlock.blockFinished)
				continue;

			//Successful insertion meaning the node wasn't visited yet
			if(networkMembers.emplace(currentIndex).second)
			{
				//We add to potential members any node coming from our own
				for(const auto & link : currentBlock.blocksRequestingData)
				{
					const size_t blockIndex = indexOfBlockID(blocks, link.block);
					assert(blockIndex < nbBlocks);
					if(!blocks[blockIndex].blockFinished)
						potentialMembers.emplace(blockIndex);
				}

				//We add to potential members any node with data for us
				for(const auto & link : currentBlock.blocksWithDataForCurrent)
				{
					//We may be pulling data from outside the network
					const size_t blockIndex = indexOfBlockID(blocks, link.block);
					if(blockIndex < nbBlocks && !blocks[blockIndex].blockFinished)
						potentialMembers.emplace(indexOfBlockID(blocks, link.block));
				}
			}
		}

		output.clear();
		output.reserve(networkMembers.size());
		output.insert(output.begin(), networkMembers.begin(), networkMembers.end());
		sort(output.begin(), output.end());
	}
}

uint8_t numberOfBitsNecessary(size_t x)
{
	uint8_t output = 0;

	while(x)
	{
		x >>= 1;
		output += 1;
	}

	return output;
}

uint64_t largestPossibleValue(uint8_t numberOfBits)
{
	return (1u << numberOfBits) - 1u;
}

void dumpCommands(const vector<PublicCommand> & commands, const char *string)
{
	FILE * output = stdout;

	if(string != nullptr && *string != 0)
	{
		output = fopen(string, "w+");
		if(output == nullptr)
			output = stdout;
	}

	for(const auto & command : commands)
		Command(command).print(output);
}

void BSDiff::DynamicArray::freeData()
{
	if (length)
		free(data);

	data = nullptr;
	length = 0;
}

bool operator>(const BlockID & a, const Block & b) { return b < a;	}
bool operator<(const BlockID & a, const Block & b) { return b > a;	}

//NetworkToken are composed of multiple sub-Token. The data referred by those token isn't necessarily unique
size_t NetworkToken::overlapWith(const NetworkToken & networkToken) const
{
	vector<Token> originalTokens(getTokenSortedByOrigin()), externalToken(networkToken.getTokenSortedByOrigin());

	//We iterate through originalToken to find any overlap
	size_t output = 0;
	for(auto extToken = externalToken.cbegin(), iter = originalTokens.cbegin(); extToken != externalToken.cend() && iter != originalTokens.cend(); )
	{
		//We have an overlap, we increment the output
		if(DetailedBlockMetadata::overlapWith(extToken->origin, extToken->length, iter->origin, iter->length))
		{
			const size_t endExternal = extToken->origin.getAddress() + extToken->length;
			const size_t endIter = iter->origin.getAddress() + iter->length;
			output += MIN(endExternal, endIter) - MAX(extToken->origin.getAddress(), iter->origin.getAddress());

			//Have we hit the end of the current external token?
			if(endExternal < endIter)
			{
				extToken += 1;
			}
			else if(endExternal > endIter)
			{
				iter += 1;
			}
			else
			{
				extToken += 1;
				iter += 1;
			}
		}
		else if(extToken->origin < iter->origin)
		{
			extToken += 1;
		}
		else
		{
			iter += 1;
		}
	}

	return output;
}

size_t NetworkToken::removeOverlapWith(const vector<NetworkToken> & networkToken)
{
	/*
	 * We will fill a DetailedBlock with our token, then untag any token in networkToken.
	 * In order not to add too many segments, we will ignore token clearly out of bound
	 */

	DetailedBlock detailedBlock;
	Address firstAddress = sourceToken.front().origin;
	Address lastAddress = firstAddress;

	//We add our token
	for(const auto token : sourceToken)
	{
		detailedBlock.insertNewSegment(token.getReverse());

		if(token.origin < firstAddress)
			firstAddress = token.origin;

		const Address & tokenEnd = token.origin + token.length;
		if(tokenEnd > lastAddress)
			lastAddress = tokenEnd;
	}

	//We add some of networkToken's
	for(const auto & refToken : networkToken)
	{
		for(const auto & token : refToken.sourceToken)
		{
			if(token.origin >= lastAddress)
				continue;

			const Address & address = token.origin + token.length;
			if(address < firstAddress)
				continue;

			detailedBlock.insertNewSegment(DetailedBlockMetadata(token.getReverse(), false));
		}
	}

	//We can now rebuild our sourceToken
	vector<Token> newSourceToken;
	size_t sumLength = 0;
	for(const auto & segment : detailedBlock.segments)
	{
		if(segment.tagged)
		{
			//We cancel the previous call to getReverse
			newSourceToken.emplace_back(Token(segment.source, segment.length, segment.destination));
			sumLength += segment.length;
		}
	}

	assert(sumLength <= length);

	const size_t change = length - sumLength;
	if(change)
	{
		sourceToken = newSourceToken;
		length = sumLength;
	}

	return change;
}
