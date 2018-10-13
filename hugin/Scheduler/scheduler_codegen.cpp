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

DetailedBlock extractDataNecessaryInSecondary(const VirtualMemory & memoryLayout, const vector<NetworkNode> &nodes, const BlockID & toLoad)
{
	DetailedBlock necessaryDataInSecondary;

	for(const auto & node : nodes)
	{
		for(const auto & netToken : node.tokens)
		{
			for(const auto & token : netToken.sourceToken)
			{
				size_t shift = 0;
				memoryLayout.iterateTranslatedSegments(token.origin, token.length, [&](const Address& from, const size_t length) {
					if(from == toLoad)
						necessaryDataInSecondary.insertNewSegment(DetailedBlockMetadata(token.origin + shift, length, true));
					
					shift += length;
				});
			}
		}
	}

	//We remove unnecessary separations between multiple segments
	necessaryDataInSecondary.compactSegments();

	return necessaryDataInSecondary;
}

/*
 * Half Swap algorithm principle:
 *
 * This algorithm is used when a cycle of more than two items is detected.
 * The problem it solves is having to keep track of discarded data from one block that are necessary to another (the one that is pulling data from this block)
 * 		without requiring more than a block worth in memory for execution (theoritical minimum, as we must erase a full block is order to write anything, requiring a block worth of backup memory)
 * The naive approch requires up to 2x (1.5x after some optimizations) the size of a block in memory in simple cycle cases (in order to save this data while loading the block
 * 		we're about to erase in memory as its content may be used by the next block in the chain).
 *
 * In our configuration, as illustrated below, block B need `b` bytes of data from bloc A, etc...
 *
 * 	(A) - b -> (B)
 * 	 ↑          |
 * 	 |          c
 * 	 a          |
 * 	 |          ↓
 * 	(D) <- d - (C)
 *
 * 	HS works by first looking for a specific patern and work on it. Specifically, three blocks (for instance, A, B and C) where the link
 * 		between the first two (b) is larger than the second (c). In this case, Half swap can change the patern to the following,
 * 		without any data left in the temporary buffer
 *
 * 	(A) <- ≤ b - (B)
 * 	 ↑ \
 * 	 |  \
 * 	 a   ↳ c --.
 * 	 |          ↓
 * 	(D) <- d - (C)
 *
 * 	This new configuration effectively remove B from the cycle and thus the algorithm can be executed again until only two blocks are left in the cycle,
 * 		which can be solved with a much simple swap algorithm.
 *
 * 	The spirit of half swap is to load B in memory, wipe the block and write the final version of B (parts of B + b)
 * 	We then defragment the temporary buffer and only keep c, then load what data from A that wasn't copied in B (A - b) in the buffer
 * 	We can then wipe A and write the content of the temporary buffer to it.
 * 	Because b > c, we can be sure to have the room, possibly leaving a couple of unused bytes
 * 	This algorithm however requires a robust virtual memory system to keep track of c being moved and defragmented from B to A, and parts of b that was expected in A but was moved to B.
 */

//First has the largest outgoing link
void Scheduler::halfSwapCodeGeneration(NetworkNode & firstNode, NetworkNode & secNode, VirtualMemory & memoryLayout, SchedulerData & commands)
{
	/*
	 *	Although it would be pretty nice, we can't enable the node swap for HSwap in the current state of things.
	 * This is because we don't really know what content goes where, and thus how to simulate flushCacheToBlock before having loaded everything.
	 * This could be fixed by reimplementing performToken, or simply use it at some computational cost when building the update.
	 */
	Scheduler::partialSwapCodeGeneration(firstNode, secNode, [&](const BlockID & block, bool, VirtualMemory& memoryLayout, SchedulerData& commands)
	{
		commands.insertCommand({ERASE, block});

		if(block == secNode.block)
			memoryLayout.writeTaggedToBlock(block, secNode.blockFinalLayout, commands);
		else
			memoryLayout.flushCacheToBlock(block, true, commands);

	}, memoryLayout, commands, false);
}

void Scheduler::partialSwapCodeGeneration(const NetworkNode & firstNode, const NetworkNode & secNode, PerformCopy performCopy, VirtualMemory & memoryLayout, SchedulerData & commands, bool canReorder)
{
	BlockID firstBlockID = firstNode.block, secondBlockID = secNode.block;

	//If we have a cached write and also write to the same block, we can merge those two writes
	bool cacheAlreadyLoaded = false, didReverse = false;
	if(memoryLayout.hasCachedWrite)
	{
		if(memoryLayout.cachedWriteBlock == firstBlockID && canReorder)
		{
			BlockID tmp = secondBlockID;
			secondBlockID = firstBlockID;
			firstBlockID = tmp;

			memoryLayout.dropCachedWrite();
			cacheAlreadyLoaded = true;
			didReverse = true;
		}
		else if(memoryLayout.cachedWriteBlock == secondBlockID)
		{
			memoryLayout.dropCachedWrite();
			cacheAlreadyLoaded = true;
		}
		else
			memoryLayout.commitCachedWrite(commands);
	}

	//If we're not merging the writes, we load the data to keep in the cache
	if(!cacheAlreadyLoaded)
	{
		memoryLayout.commitCachedWrite(commands);

		DetailedBlock necessaryDataInSecondary = extractDataNecessaryInSecondary(memoryLayout, {firstNode, secNode}, secondBlockID);
		memoryLayout.flushCache();

		//We can load the data in CACHE_BUF
		memoryLayout.loadTaggedToTMP(necessaryDataInSecondary, commands);
	}

	//We then write back the data of second
	performCopy(secondBlockID, didReverse, memoryLayout, commands);

	//We check if there is any data in the cache. Otherwise, the next write is pointless
	//	This check is skipped if we know we're going to have to go through this write anyway (if we're writing the final version)
	bool havePendingWrites = (didReverse ? secNode : firstNode).isFinal;
	if(!havePendingWrites)
	{
		for(const auto & segment : memoryLayout.cacheLayout.segments)
		{
			if(segment.tagged)
			{
				havePendingWrites = true;
				break;
			}
		}
	}

	if(havePendingWrites)
	{
		//Find the data we need to save from first
		DetailedBlock largerNodeMeta = extractDataNecessaryInSecondary(memoryLayout, {(didReverse ? secNode : firstNode)}, firstBlockID);

		//Load it in the cache
		memoryLayout.loadTaggedToTMP(largerNodeMeta, commands);

		//We then erase and write `first`
		performCopy(firstBlockID, didReverse, memoryLayout, commands);
	}
}

void Scheduler::reorderingSwapCodeGeneration(NetworkNode & firstNode, NetworkNode & secondaryNode, VirtualMemory & memoryLayout, SchedulerData & commands)
{
	BlockID first = firstNode.block, second = secondaryNode.block;
	bool cacheAlreadyLoaded = false, didReverse = false;
	if(memoryLayout.hasCachedWrite)
	{
		if(memoryLayout.cachedWriteBlock == first && !firstNode.isFinal)
		{
			memoryLayout.dropCachedWrite();
			cacheAlreadyLoaded = true;
		}
		else if(memoryLayout.cachedWriteBlock == second)
		{
			auto tmp = second;
			second = first;
			first = tmp;

			memoryLayout.dropCachedWrite();
			cacheAlreadyLoaded = true;
			didReverse = true;
		}
		else
			memoryLayout.commitCachedWrite(commands);
	}

	bool isFirstFinal = didReverse ? secondaryNode.isFinal : firstNode.isFinal;
	if(!cacheAlreadyLoaded && !isFirstFinal)
	{
		//Load the first block to TMP and erase it
		DetailedBlock necessaryDataInSecondary = extractDataNecessaryInSecondary(memoryLayout, {firstNode, secondaryNode}, first);

		//We can load the data in CACHE_BUF
		memoryLayout.loadTaggedToTMP(necessaryDataInSecondary, commands);

		//All the data being loaded, we can flush the page corresponding to secondaryNode
		commands.insertCommand({ERASE, first});
	}

	//We write the block (if relevant)
	if(!isFirstFinal)
		memoryLayout.writeTaggedToBlock(first, (didReverse ? secondaryNode : firstNode).blockFinalLayout, commands);

	//We then decide which data is interesting
	DetailedBlock dataToLoad = extractDataNecessaryInSecondary(memoryLayout, {didReverse ? firstNode : secondaryNode}, second);

#ifdef IGNORE_CACHE_LAYOUT
	//We really don't care about the layout in the cache of the data we're loading so we're offering an opportunity for sequential load
	sort(dataToLoad.segments.begin(), dataToLoad.segments.end(), [](const DetailedBlockMetadata & a, const DetailedBlockMetadata & b) {	return a.source < b.source;	});
	dataToLoad.sorted = false;
#endif

	//We load data from the secondary buffer in the temporary buffer
	memoryLayout.loadTaggedToTMP(dataToLoad, commands);

	//Copy back the data from the temporary buffer
	commands.insertCommand({ERASE, second});
	memoryLayout.writeTaggedToBlock(second, didReverse ? firstNode.blockFinalLayout : secondaryNode.blockFinalLayout, commands);

#ifdef VERY_AGGRESSIVE_ASSERT
	for(const auto & segment : memoryLayout.cacheLayout.segments)
		assert(!segment.tagged);
#endif
}

void Scheduler::networkSwapCodeGeneration(const NetworkNode & firstNode, const NetworkNode & secNode, VirtualMemory & memoryLayout, SchedulerData & commands)
{
	//We try to reuse as much code from the original halfswap implementation as possible
	Scheduler::partialSwapCodeGeneration(firstNode, secNode, [&](const BlockID & block, bool didReverse, VirtualMemory& memoryLayout, SchedulerData& commands)
	{
		//We erase the page we're about to write
		commands.insertCommand({ERASE, block});

		const NetworkNode & curNode = block == firstNode.block ? firstNode : secNode;
		const bool firstWrite = block == (didReverse ? firstNode.block : secNode.block);

		if(firstWrite)
		{
			memoryLayout.writeTaggedToBlock(curNode.block, curNode.compileLayout(), commands, !curNode.isFinal);
		}
		else
		{
			if(curNode.isFinal)
			{
				memoryLayout.writeTaggedToBlock(curNode.block, curNode.compileLayout(), commands, false);

#ifdef VERY_AGGRESSIVE_ASSERT
				for(const auto & segment : memoryLayout.cacheLayout.segments)
					assert(!segment.tagged);
#endif
			}
			else
				memoryLayout.cacheWrite(curNode.block, curNode.compileLayout(), true, commands);
		}
	}, memoryLayout, commands, true);
}

void Scheduler::pullDataToNode(const NetworkNode & node, VirtualMemory & memoryLayout, SchedulerData & commands)
{
	if(memoryLayout.hasCachedWrite)
		memoryLayout.commitCachedWrite(commands);

	//Load the first block to TMP and erase it
	DetailedBlock necessaryDataInSecondary = extractDataNecessaryInSecondary(memoryLayout, {node}, node.block);

	//We can load the data in CACHE_BUF
	memoryLayout.loadTaggedToTMP(necessaryDataInSecondary, commands);

	//All the data being loaded, we can flush the page corresponding to the node
	commands.insertCommand({ERASE, node.block});

	//Then write the final layout
	memoryLayout.writeTaggedToBlock(node.block, node.compileLayout(), commands, false);
}
