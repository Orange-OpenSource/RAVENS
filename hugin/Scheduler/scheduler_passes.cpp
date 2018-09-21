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
#include "scheduler.h"

namespace Scheduler
{
	void removeSelfReferencesOnly(vector<Block> & blocks, SchedulerData & commands)
	{
		for(Block & block : blocks)
		{
			if(block.blockFinished)
				continue;

			//If no external (incoming or outgoing) references, that's an easy case
			if(block.blocksWithDataForCurrent.empty() && block.blocksRequestingData.empty())
			{
				//If we have internal references (moves), we perform them before discarding the block
				if(block.blockNeedSwap)
					interpretBlockSort(block, commands);

				block.blockFinished = true;
			}
		}
	}

	void removeUnidirectionnalReferences(vector<Block> & blocks, SchedulerData & commands)
	{
		//We loop in case of a chain (a <- b <- c)
		//	In this case, C will be scheduled on the first pass and b on the second, as b wasn't free when first evaluated

		commands.insertCommand({REBASE, 0x0, 0});

		bool foundChange;
		do
		{
			foundChange = false;
			for(Block & block : blocks)
			{
				if(block.blockFinished)
					continue;

				//If no incoming references (we pull data from other blocks but nobody need ours, that's an easy case)
				if(block.blocksRequestingData.empty())
				{
					commands.newTransaction();

					if(block.blockNeedSwap)
						interpretBlockSort(block, commands, false);
					else
						commands.insertCommand({ERASE, block.blockID});

					for(const auto & token : block.data)
					{
						//Token already dealt with by interpretBlockSort
						if(token.origin == block.blockID)
							continue;

						commands.insertCommand({COPY, token.origin, token.length, token.finalAddress});
					}

					commands.finishTransaction();
					block.blockFinished = true;
					foundChange = true;

					//We release the blocks we were referring to by remove the reference in their blocksRequestingData
					for(const BlockLink & blockID : block.blocksWithDataForCurrent)
					{
						size_t index = indexOfBlockID(blocks, blockID);
						if(index < blocks.size())
						{
							auto & currentBlockArray = blocks[index].blocksRequestingData;

							//If we had multiple references toward the same block, we may try to delete the record multiple times
							auto position = find(currentBlockArray.begin(), currentBlockArray.end(), block.blockID);
							if (position != currentBlockArray.end())
								currentBlockArray.erase(position);
						}
						//`else` The block we're copying data from is outside of the range of the new file, nothing to do
					}
				}
			}
		} while(foundChange);

		commands.updateLastRebase();
	}

	void removeNetworks(vector<Block> & blocks, SchedulerData & commands)
	{
		const size_t length = blocks.size();
		size_t counter = 0;

		for (size_t i = 0; i < length; ++i)
		{
			if (blocks[i].blockFinished)
				continue;

			commands.insertCommand({REBASE, 0x0, 0});

			vector<size_t> blockNetwork;
			extractNetwork(blocks, i, blockNetwork);

			if(blockNetwork.empty())
			{
				continue;
			}

			Network network(blocks, blockNetwork);

			while(network.performBestSwap(commands))
				counter += 1;

			network.performFinalFlush(commands);

			for(const size_t index : blockNetwork)
				blocks[index].blockFinished = true;

			commands.updateLastRebase();
		}

		if(commands.wantLog && counter > 0)
			printf("Network solved in %zu iterations\n", counter);
	}
}
