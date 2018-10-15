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
#include <decoding/decoder_config.h>

void Command::performTrivialOptimization()
{
#ifdef CODEGEN_OPTIMIZATIONS
	if(command == COPY && length == BLOCK_SIZE && mainBlockOffset == 0 && secondaryOffset == 0)
		{
			if(secondaryBlock != CACHE_BUF && mainBlock == CACHE_BUF)
			{
				command = COMMIT;
				mainBlock = secondaryBlock;
				secondaryBlock = 0;
				length = 0;
			}
		}
#endif
}
bool Command::couldConvertToChainCopyWithPrevious(const Command & prev, const Address & endPreviousCopy) const
{
	//We're not a copy
	if(command != COPY)
		return false;

	//The previous command is an eligible COPY
	if(prev.command == COPY && secondaryBlock == prev.secondaryBlock)
	{
		const size_t previousEnd = prev.secondaryOffset + prev.length;

		if(previousEnd > secondaryOffset)
			return false;

		return secondaryOffset - previousEnd <= MAX_SKIP_LENGTH;
	}
	else if(prev.command == CHAINED_COPY && endPreviousCopy.getBlock() == secondaryBlock)
	{
		if(endPreviousCopy.getOffset() > secondaryOffset)
			return false;

		return secondaryOffset - endPreviousCopy.getOffset() <= MAX_SKIP_LENGTH;
	}

	//Considering we're writting to a block and previous start at the beginning of the block, writes can't overlap
	if(prev.command == FLUSH_AND_PARTIAL_COMMIT && prev.mainBlock == secondaryBlock)
	{
		return secondaryOffset - prev.length <= MAX_SKIP_LENGTH;
	}

	return false;
}

void SchedulerData::insertCommand(Command command)
{
	command.performTrivialOptimization();

	if(command.command == COPY && command.length == 0)
		return;

#ifdef CODEGEN_OPTIMIZATIONS
	if(command.command == REBASE)
	{
		if(!commands.empty() && commands.back().command == REBASE)
			return;
	}

	else if(!commands.empty() && (commands.back().command != REBASE || commands.size() > 1))
	{
		//Rebases are irrelevant to the scheduler and the instruction generation, and thus should be ignored for optimizations
		Command & prev = commands.back().command == REBASE ? commands[commands.size() - 2] : commands.back();

		//We should never have two consecutive ERASE for different pages
		if(command.isEraseLike() && prev.isEraseLike(true))
			assert(command.mainBlock == prev.mainBlock);

		//Do we really need to insert a new command?
		if(prev.mainBlock == command.mainBlock)
		{
			//Can we extend the previous COPY ?
			if(prev.command == COPY && command.command == COPY
			   && prev.secondaryBlock == command.secondaryBlock
			   && prev.mainBlockOffset + prev.length == command.mainBlockOffset
			   && prev.secondaryOffset + prev.length == command.secondaryOffset)
			{
				if(transactionInProgress)
				{
					prev.length += command.length;
					prev.performTrivialOptimization();

					//If not a copy anymore (likely a COMMIT), we perform a new insertion optimization pass
					if(prev.command != COPY)
					{
						commands.pop_back();
						insertCommand(Command(prev));
					}
					return;
				}
			}

				// Loading the content of a block we just commited is pointless
			else if(command.isLoad() && prev.isCommitLike())
			{
				return;
			}

				//We are erasing what we just wrote is pointless
			else if(command.command == ERASE && prev.isCommitLike())
			{
				//Does the previous instruction had side effects
				if(prev.command == COMMIT)
					commands.pop_back();
				else
					prev.command = ERASE;

				return;
			}

				//LOAD followed by an ERASE has a monolithic instruction
			else if(command.command == ERASE && prev.isLoad())
			{
				prev.command = LOAD_AND_FLUSH;
				prev.secondaryBlock = prev.secondaryOffset = 0;
				prev.length = 0;
				return;
			}

				//ERASE followed by an COMMIT has a monolithic instruction
			else if(command.command == COMMIT && prev.command == ERASE)
			{
				prev.command = FLUSH_AND_PARTIAL_COMMIT;
				prev.length = BLOCK_SIZE;
				return;
			}

				//COMMIT followed by a LOAD and then followed by an ERASE is useless
			else if(command.command == LOAD_AND_FLUSH && prev.isCommitLike())
			{
				//Side effects?
				if(prev.command == FLUSH_AND_PARTIAL_COMMIT && prev.length == BLOCK_SIZE)
					prev.command = ERASE;
				return;
			}

			else if(command.command == COMMIT && prev.command == LOAD_AND_FLUSH)
			{
				commands.pop_back();
				return;
			}

			else if(command.command == ERASE && prev.command == ERASE)
			{
				return;
			}
		}
		else if(command.mainBlock == CACHE_BUF)
		{
			//ERASE followed by an COPY from the beginning of the cache has a monolithic instruction
			if(prev.command == ERASE && command.command == COPY && command.mainBlockOffset == 0 &&
			   command.secondaryBlock == prev.mainBlock && command.secondaryOffset == 0)
			{
				prev.command = FLUSH_AND_PARTIAL_COMMIT;
				prev.length = command.length;
				return;
			}
			else if(command.command == COPY && prev.command == FLUSH_AND_PARTIAL_COMMIT && command.secondaryBlock == prev.mainBlock
					&& command.mainBlockOffset == command.secondaryOffset && command.mainBlockOffset == prev.length)
			{
				prev.length += command.length;
				return;
			}
		}
	}
#endif

	if(!transactionInProgress)
		currentTransaction += 1;

	command.transactionID = currentTransaction;
	commands.emplace_back(command);
}

void SchedulerData::createChains()
{
	vector<Command> newCommands;
	newCommands.reserve(commands.size());

	Address endPreviousCopy(0);

	for(auto command : commands)
	{
		//Generate chained copies
		if(command.couldConvertToChainCopyWithPrevious(newCommands.back(), endPreviousCopy))
		{
			//We know we copy to the same block
			const ssize_t delta = command.secondaryOffset - endPreviousCopy.getOffset();

			if(delta != 0)
			{
				assert(delta <= MAX_SKIP_LENGTH);

				Command skip(ERASE, 0);

				skip.command = CHAINED_COPY_SKIP;
				skip.length = (size_t) delta;
				endPreviousCopy += skip.length;

				newCommands.emplace_back(skip);
			}

			command.command = CHAINED_COPY;
			command.secondaryBlock = command.secondaryOffset = 0;
			endPreviousCopy += command.length;
		}
		else if(command.command == COPY)
		{
			endPreviousCopy = Address(command.secondaryBlock, command.secondaryOffset + command.length);
		}
		else if(command.command == FLUSH_AND_PARTIAL_COMMIT)
		{
			endPreviousCopy = Address(command.mainBlock, command.length);
		}

		newCommands.emplace_back(command);
	}

	commands.clear();
	commands.reserve(newCommands.size());
	commands.insert(commands.begin(), newCommands.begin(), newCommands.end());
}

void SchedulerData::commitUseBlockSection(vector<Command> &newCommands, size_t instructionIgnore, bool &hadBlock, const BlockID &blockChain, vector<Command>::const_iterator &startChain, const vector<Command>::const_iterator &iter) const
{
	//Determine whether we would benefit from using USE_BLOCK/RELEASE_BLOCK
	bool useInstruction = static_cast<size_t>(abs(iter - startChain)) >= BLOCK_USE_THRESHOLD + instructionIgnore;

	if(useInstruction)
	{
		newCommands.emplace_back(Command(USE_BLOCK, blockChain));
	}
	else if(hadBlock)
	{
		newCommands.emplace_back(Command(RELEASE_BLOCK));
		hadBlock = false;
	}

	while(startChain < iter)
		newCommands.push_back(*(startChain++));

	if(useInstruction)
		hadBlock = true;
}

void SchedulerData::addUseBlocks()
{
	vector<Command> newCommands;
	newCommands.reserve(commands.size());

	size_t instructionIgnore = 0;
	bool hasBlockChain = false, hadBlock = false, isStart = true;
	BlockID blockChain = 0;
	auto startChain = commands.cbegin();

	for(auto iter = commands.cbegin(); iter != commands.cend(); ++iter)
	{
		switch (iter->command)
		{
			case ERASE:
			case COMMIT:
			case FLUSH_AND_PARTIAL_COMMIT:
			case LOAD_AND_FLUSH:
			case CHAINED_COPY:
			case COPY:
			{
				BlockID currentBlock = iter->mainBlock;
				if(iter->command == COPY && currentBlock == CACHE_BUF)
					currentBlock = iter->secondaryBlock;

				if(hasBlockChain && currentBlock != blockChain)
				{
					//Cache to cache copies are ignored
					if(currentBlock != CACHE_BUF)
					{
						commitUseBlockSection(newCommands, instructionIgnore, hadBlock, blockChain, startChain, iter);
						hasBlockChain = false;
					}
					else
						instructionIgnore += 1;
				}

				if(!hasBlockChain && currentBlock != CACHE_BUF)
				{
					startChain = iter;
					isStart = false;
					hasBlockChain = true;
					blockChain = currentBlock;
					instructionIgnore = 0;
				}

				break;
			}

			case REBASE:
			{
				if(isStart)
					newCommands.push_back(*iter);
			}
			case CHAINED_COPY_SKIP:
			{
				//This instruction doesn't encode the block ID and thus doesn't benefit from USE_BLOCK
				if(hasBlockChain)
					instructionIgnore += 1;
				break;
			}

				//Aren't supposed to exist at this point
			case END_OF_STREAM:
			case USE_BLOCK:
			case RELEASE_BLOCK:break;
		}
	}

	if(hasBlockChain)
		commitUseBlockSection(newCommands, instructionIgnore, hadBlock, blockChain, startChain, commands.cend());

	commands.clear();
	commands.reserve(newCommands.size());
	commands.insert(commands.begin(), newCommands.begin(), newCommands.end());
}

void SchedulerData::updateLastRebase()
{
	if(commands.empty())
		return;

	bool stop = false;
	BlockID smallestBlock = CACHE_ADDRESS, largestBlock = 0;
	for(auto iter = commands.end() - 1; !stop; --iter)
	{
		if(iter == commands.begin())
			stop = true;

		switch (iter->command)
		{
			//Cases are chained. Later cases will check mainBlock
			case COPY:
			{
				if(iter->secondaryBlock != CACHE_BUF)
				{
					if(iter->secondaryBlock < smallestBlock)
						smallestBlock = iter->secondaryBlock;

					if(iter->secondaryBlock > largestBlock)
						largestBlock = iter->secondaryBlock;
				}
			}

				//Only check the main block if not cache
			case CHAINED_COPY:
			{
				if(iter->mainBlock == CACHE_BUF)
					break;
			}
			case ERASE:
			case COMMIT:
			case FLUSH_AND_PARTIAL_COMMIT:
			case LOAD_AND_FLUSH:
			{
				if(iter->mainBlock < smallestBlock)
					smallestBlock = iter->mainBlock;

				if(iter->mainBlock > largestBlock)
					largestBlock = iter->mainBlock;

				break;
			}

			case REBASE:
			{
				//Configure the new rebase
				iter->mainBlock = smallestBlock;

				//iter->length is for now the number of blocks impacted by this REBASE
				iter->length = (largestBlock.value - smallestBlock.value) >> BLOCK_SIZE_BIT;
				stop = true;

				if(smallestBlock.value > largestBlock.value)
				{
					commands.erase(iter);
					break;
				}

				if(iter == commands.begin())
					break;

				auto newIter = iter;

				//Look for the previous rebase
				while(--iter != commands.begin() && iter->command != REBASE);

				//We check if adding a rebase is relevant. If not using the same length, we'd need to evaluate precisely the compression advantage of each rebase...
				if(iter->command == REBASE)
				{
					//If both are roughly equivalents
					if(numberOfBitsNecessary(iter->length) == numberOfBitsNecessary(newIter->length))
					{
						//Not identical
						if(iter->mainBlock != newIter->mainBlock)
						{
							//Merge both REBASE if that wouldn't use an additional bit to store BlockID
							BlockID min = MIN(iter->mainBlock, newIter->mainBlock);
							BlockID max = MAX(iter->mainBlock + (iter->length - 1), newIter->mainBlock + (newIter->length - 1));
							size_t necessaryLength = (max.value - min.value) >> BLOCK_SIZE_BIT;

							if(numberOfBitsNecessary(iter->length) == numberOfBitsNecessary(necessaryLength))
							{
								iter->mainBlock = min;
								iter->length = necessaryLength;
								commands.erase(newIter);
							}
						}
						else
							commands.erase(newIter);
					}
				}
					//If this is the first REBASE, then USE_BLOCK is more relevant
				else if(newIter->length == 1)
					commands.erase(newIter);

				break;
			}

				//Irrelevant
			case CHAINED_COPY_SKIP:
			case USE_BLOCK:
			case RELEASE_BLOCK:
			case END_OF_STREAM:
				break;
		}
	}
}
