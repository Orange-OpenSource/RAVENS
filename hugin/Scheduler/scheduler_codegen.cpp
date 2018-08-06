//
// Created by Emile-Hugo Spir on 3/16/18.
//

#include "scheduler.h"

DetailedBlock extractDataNecessaryInSecondary(const VirtualMemory & memoryLayout, const NetworkNode &first, const NetworkNode &second, const BlockID & toLoad)
{
	DetailedBlock necessaryDataInSecondary;

	for(const auto & node : {first, second})
	{
		for(const auto & netToken : node.tokens)
		{
			for(const auto & token : netToken.sourceToken)
			{
				vector<DetailedBlockMetadata> translation;
				memoryLayout.translateSegment(token.origin, token.length, translation);

				for(const auto &segment: translation)
				{
					if(segment.source == toLoad)
						necessaryDataInSecondary.insertNewSegment(DetailedBlockMetadata(segment.source, segment.length, true));
				}
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
 * 		without requiring more than a block worth in memory for execution (theoritical minimum, as we must erase a full block is order to write anything)
 * The naive approch requires up to 2x (1.5x after some optimizations) the size of a block in memory (in order to save this data while loading the block
 * 		we're about to erase in memory as its content may be used by the next block in the chain.
 *
 * In our configuration, as illustrated below, block B need `b` bytes of data from bloc A, etc...
 *
 * 	(A) - b -> (B)
 * 	 ^			|
 * 	 |			c
 * 	 a			|
 * 	 |			v
 * 	(D) <- d - (C)
 *
 * 	HS works by first looking for a specific patern and work on it. Specifically, three blocks (for instance, A, B and C) where the link
 * 		between the first two (b) is larger than the second (c). In this case, Half swap can change the patern to the following,
 * 		without any data left in the temporary buffer
 *
 * 	(A) <- ≤ b - (B)
 * 	 ^ \
 * 	 |	\
 * 	 a	 -> c --.
 * 	 |			v
 * 	(D) <- d - (C)
 *
 * 	This new configuration effectively remove B from the cycle and thus the algorithm can be executed again until only two blocks are left in the cycle,
 * 		which can be solved with a much simple swap algorithm.
 *
 * 	The spirit of half swap is to load B in memory, wipe the block and write the final version of B (parts of B + b)
 * 	We then defragment the temporary buffer and only keep c, then load what data from A that wasn't copied in B (A - b) in the buffer
 * 	We can then wipe A and write the content of the temporary buffer.
 * 	Because b > c, we can be sure to have the room, possibly leaving a couple of unused bytes
 * 	This algorithm however requires a robust virtual memory system to keep track of c being moved and defragmented from B to A
 */

//First has the largest link
void Scheduler::halfSwapCodeGeneration(NetworkNode & firstNode, NetworkNode & secNode, VirtualMemory & memoryLayout, SchedulerData & commands)
{
	Scheduler::partialSwapCodeGeneration(firstNode, secNode, [&](const BlockID & block, bool, VirtualMemory& memoryLayout, SchedulerData& commands)
	{
		commands.insertCommand({ERASE, block});

		//partialSwapCodeGeneration isn't allowed to swap the two nodes so we don't need to care about that
		//FIXME: make swap of nodes possible
		if(block == secNode.block)
		{
			memoryLayout.writeTaggedToBlock(block, secNode.blockFinalLayout, commands);
		}
		else
		{
			//This is tricky, we need to look in the cache to get all the virtual addresses currently loaded.
			//	Then, we build a DetailedBlock and use it to write the block

			DetailedBlock writeCommands(block);
			buildWriteCommandToFlushCacheFromNodes({firstNode, secNode}, memoryLayout, block, writeCommands);
			memoryLayout.cacheWrite(block, writeCommands, true, commands);
		}

	}, [&](bool didReverse)
	{
		//We tag for backup any relevant data from out node
		const NetworkNode & curNode = didReverse ? secNode : firstNode;
		DetailedBlock largerNodeMeta(curNode.block);

		//We add the data that are required by someone else
		for(const auto & netToken : curNode.tokens)
		{
			//We ignore token toward secNode (already copied)
			if(netToken.cleared)
				continue;

			//The tokens are either already located on our page and need backup, or are already on the cache
			for(const auto & token : netToken.sourceToken)
				largerNodeMeta.insertNewSegment(DetailedBlockMetadata(token.origin, token.length, true));
		}
		return largerNodeMeta;
	}, memoryLayout, commands, false);
}

void Scheduler::partialSwapCodeGeneration(const NetworkNode & firstNode, const NetworkNode & secNode, SwapOperator swapOperator, FirstNodeLayoutGenerator firstNodeLayout, VirtualMemory & memoryLayout, SchedulerData & commands, bool canReorder)
{
	BlockID first = firstNode.block, second = secNode.block;

	//If we have a cached write and also write to the same block, we can merge those two writes
	bool cacheAlreadyLoaded = false, didReverse = false;
	if(memoryLayout.hasCachedWrite)
	{
		if(memoryLayout.cachedWriteBlock == first && canReorder)
		{
			BlockID tmp = second;
			second = first;
			first = tmp;

			memoryLayout.dropCachedWrite();
			cacheAlreadyLoaded = true;
			didReverse = true;
		}
		else if(memoryLayout.cachedWriteBlock == second)
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

		DetailedBlock necessaryDataInSecondary = extractDataNecessaryInSecondary(memoryLayout, firstNode, secNode, second);
		memoryLayout.flushCache();

		//We can load the data in TMP_BUF
		memoryLayout.loadTaggedToTMP(necessaryDataInSecondary, commands, true);
	}

	//We then write back the data of second
	swapOperator(second, didReverse, memoryLayout, commands);

	//We check if there is any data in the cache. Otherwise, the next write is pointless
	bool havePendingWrites = false;
	for(const auto & segment : memoryLayout.tmpLayout.segments)
	{
		if(segment.tagged)
		{
			havePendingWrites = true;
			break;
		}
	}

	//If we're writing the final version, we override
	if(havePendingWrites || (didReverse ? secNode : firstNode).isFinal)
	{
		//Find the data we need to save from first
		DetailedBlock largerNodeMeta = firstNodeLayout(didReverse);

		//Load it in the cache
		memoryLayout.loadTaggedToTMP(largerNodeMeta, commands);

		//We then erase and write `first`
		swapOperator(first, didReverse, memoryLayout, commands);
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
		DetailedBlock necessaryDataInSecondary = extractDataNecessaryInSecondary(memoryLayout, firstNode, secondaryNode, first);

		//We can load the data in TMP_BUF
		memoryLayout.loadTaggedToTMP(necessaryDataInSecondary, commands, true);

		//All the data being loaded, we can flush the page corresponding to secondaryNode
		commands.insertCommand({ERASE, first});
	}

	//We write the block (if relevant)
	if(!isFirstFinal)
		memoryLayout.writeTaggedToBlock(first, (didReverse ? secondaryNode : firstNode).blockFinalLayout, commands);

	/// We trust the rest of the code to empty the cache it used. This is supposed to be a closed loop so it shouldn't be a problem

	//We then decide which data is interesting
	DetailedBlock dataToLoad(second);
	for(const auto & netToken : (didReverse ? firstNode : secondaryNode).tokens)
	{
		if(netToken.cleared || netToken.destinationBlockID == first)
			continue;

		for(const auto & token : netToken.sourceToken)
		{
			vector<DetailedBlockMetadata> translations;
			memoryLayout.translateSegment(token.origin, token.length, translations);
			for(const auto & translation : translations)
			{
				//We may want to ignore segment if they're referring to duplicated data
				if(translation.source == second)
					dataToLoad.insertNewSegment(DetailedBlockMetadata(translation.source, translation.length, true));
			}
		}
	}

#ifdef IGNORE_CACHE_LAYOUT
	//We really don't care about the layout in the cache of the data we're loading so we're offering an opportunity for sequential load
	sort(dataToLoad.segments.begin(), dataToLoad.segments.end(), [](const DetailedBlockMetadata & a, const DetailedBlockMetadata & b) {	return a.source < b.source;	});
	dataToLoad.sorted = false;
#endif

	//We load data from the secondary buffer in the temporary buffer
	memoryLayout.loadTaggedToTMP(dataToLoad, commands, true);

	//Copy back the data from the temporary buffer
	commands.insertCommand({ERASE, second});
	memoryLayout.writeTaggedToBlock(second, didReverse ? firstNode.blockFinalLayout : secondaryNode.blockFinalLayout, commands);
	memoryLayout.performRedirect();

#ifdef VERY_AGGRESSIVE_ASSERT
	for(const auto & segment : memoryLayout.tmpLayout.segments)
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

		const bool firstWrite = block == (didReverse ? firstNode.block : secNode.block);

		if(firstWrite)
		{
			//Blocks may be swapped internally
			if(block == secNode.block)
				memoryLayout.writeTaggedToBlock(secNode.block, secNode.compileLayout(true), commands, !secNode.isFinal);
			else
				memoryLayout.writeTaggedToBlock(firstNode.block, firstNode.compileLayout(true), commands, !firstNode.isFinal);
		}
		else
		{
			const NetworkNode & curNode = didReverse ? secNode : firstNode;

			if(curNode.isFinal)
			{
				memoryLayout.writeTaggedToBlock(curNode.block, curNode.compileLayout(true), commands, false);
				memoryLayout.performRedirect();

#ifdef VERY_AGGRESSIVE_ASSERT
				for(const auto & segment : memoryLayout.tmpLayout.segments)
					assert(!segment.tagged);
#endif
			}
			else
				memoryLayout.cacheWrite(curNode.block, curNode.compileLayout(true), true, commands);
		}

	}, [&](bool didReverse)
	{
		const NetworkNode & curNode = didReverse ? secNode : firstNode;
		return curNode.compileLayout(false);

	}, memoryLayout, commands, true);
}

void Scheduler::pullDataToNode(const NetworkNode & node, VirtualMemory & memoryLayout, SchedulerData & commands)
{
	if(memoryLayout.hasCachedWrite)
		memoryLayout.commitCachedWrite(commands);

	//Load the first block to TMP and erase it
	DetailedBlock necessaryDataInSecondary = extractDataNecessaryInSecondary(memoryLayout, node, node, node.block);

	//We can load the data in TMP_BUF
	memoryLayout.loadTaggedToTMP(necessaryDataInSecondary, commands, true);

	//All the data being loaded, we can flush the page corresponding to the node
	commands.insertCommand({ERASE, node.block});

	//Then write the final layout
	memoryLayout.writeTaggedToBlock(node.block, node.compileLayout(true), commands, false);
}
