//
// Created by Emile-Hugo Spir on 3/21/18.
//

#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include "scheduler.h"

vector<BlockID> NetworkNode::tookOverNode(const NetworkNode & pulledNode, bool bypassBlockIDDrop)
{
	const vector<NetworkToken> & pulledTokens = pulledNode.tokens;
	auto pulledIter = pulledTokens.cbegin();

	long remove = LONG_MAX, lengthWon = LONG_MAX;
	vector<BlockID> needToLowerRefCount;	//We need to lower the refcount as we already had a link with this node when siphonning the pulledNode

	tokens.reserve(tokens.size() + pulledTokens.size() - 1);

	//We find which blocks have to be imported, which are irrelevant
	long nextShift;
	for(auto token = tokens.begin(); token != tokens.end(); token += nextShift)
	{
		nextShift = 1;

		if(token->destinationBlockID == pulledNode.block && !bypassBlockIDDrop)
		{
			remove = token - tokens.begin();
			continue;
		}

		//We reached the end of the tokens to import
		if(pulledIter == pulledTokens.cend())
		{
			if(remove == LONG_MAX)
				continue;
			else
				break;
		}
		//We found the node's self reference
		else if(pulledIter->destinationBlockID == pulledNode.block && !bypassBlockIDDrop)
		{
			pulledIter += 1;
			nextShift = 0;
			continue;
		}

		const auto & destinationBlockID = token->destinationBlockID;

		if(destinationBlockID > pulledIter->destinationBlockID)
		{
			//The block is missing
			const auto index = pulledIter - pulledTokens.cbegin();

			//We don't duplicate the insertion of our own data. lengthWon will take care of it later
			if(pulledIter->destinationBlockID != block)
			{
				const long tokenIterIndex = token - tokens.begin();

				auto newToken = *pulledIter;
				newToken.sourceBlockID = block;
				tokens.insert(token, newToken);
				sumOut += newToken.length;
				nbSourcesOut += 1;

				token = tokens.begin() + tokenIterIndex;
				nextShift = 0;
			}
			else
			{
				lengthWon = index;
				nextShift = 0;
			}

			pulledIter += 1;
		}

		else if(destinationBlockID == pulledIter->destinationBlockID)
		{
			//An existing chunk was siphoned
			token->length += pulledIter->length;
			token->sourceToken.insert(token->sourceToken.end(), pulledIter->sourceToken.begin(), pulledIter->sourceToken.end());

			//Insert in a sorted array the blocks we need to lower the source count of
			if(token->destinationBlockID != block || bypassBlockIDDrop)
			{
				needToLowerRefCount.insert(upper_bound(needToLowerRefCount.begin(), needToLowerRefCount.end(), token->destinationBlockID), token->destinationBlockID);
			}
			else
			{
				lengthWon = pulledIter - pulledTokens.cbegin();
			}

			pulledIter += 1;
		}
	}

	//Import what is left in pulledTokens
	if(pulledIter != pulledTokens.cend())
	{
		while(pulledIter < pulledTokens.cend())
		{
			//The token we just siphoned
			if(pulledIter->destinationBlockID != pulledNode.block || bypassBlockIDDrop)
			{
				//We don't duplicate the insertion of our own data. lengthWon will take care of it later
				if(pulledIter->destinationBlockID != block || bypassBlockIDDrop)
				{
					auto newToken = *pulledIter;
					newToken.sourceBlockID = block;
					tokens.insert(tokens.end(), newToken);
					sumOut += newToken.length;
					nbSourcesOut += 1;
				}
				else
					lengthWon = pulledIter - pulledTokens.cbegin();
			}

			pulledIter += 1;
		}
	}

	//We can discard references to the block we just siphoned
	if(remove != LONG_MAX)
	{
		assert(nbSourcesOut != 0);
		nbSourcesOut -= 1;
		sumOut -= tokens[remove].length;
		tokens.erase(tokens.begin() + remove);
	}

	//If some data our block was looking for was recovered
	if(lengthWon != LONG_MAX)
	{
		//We look for a previous self reference
		bool found = false;
		for(auto & token : tokens)
		{
			if(token.destinationBlockID == block)
			{
				auto & oldToken = pulledTokens[lengthWon];
				token.sourceToken.insert(token.sourceToken.end(), oldToken.sourceToken.begin(), oldToken.sourceToken.end());
				token.length += oldToken.length;
				found = true;
				break;
			}
		}

		//If the current block had no previous data for itself
		if(!found)
		{
			auto newToken = pulledTokens[lengthWon];
			newToken.sourceBlockID = block;
			tokens.insert(upper_bound(tokens.begin(), tokens.end(), newToken), newToken);
		}

		nbSourcesIn -= 1;
	}

	return needToLowerRefCount;
}

bool NetworkNode::dispatchInNodes(NetworkNode & node1, NetworkNode & node2)
{
	size_t lengthToAllocate = getOccupationLevel();
	vector<pair<size_t, size_t>> spaceLeftAfterward;
	bool needReloop, didReloopOnce = false;
	BlockID reloopSource(TMP_BUF), reloopDest(TMP_BUF);

	assert(lengthToAllocate <= 2 * BLOCK_SIZE);

	do
	{
		needReloop = false;

		//We allocate our tokens in the two nodes, while making sure we don't overload them
		for(char counter = 0; counter < 2; ++counter)
		{
			//Sort the token to dispatch, as removeOverlapWithToken may have reduced some's length
			sort(tokens.begin(), tokens.end(), [](const NetworkToken & a, const NetworkToken & b) { return a.length > b.length; });

			auto &node = counter == 0 ? node1 : node2;

			const size_t nodeCurrentOccupation = node.getOccupationLevel();
			size_t spaceLeft = BLOCK_SIZE - nodeCurrentOccupation;

			assert(nodeCurrentOccupation <= BLOCK_SIZE);
			
			//We check whether the token length is consistent with the occupation level (otherwise, this would imply internal duplication)
			if(node.tokens.front().length != (BLOCK_SIZE - spaceLeft))
			{
				assert(node.tokens.size() == 1);

				//It looks like have some duplicate subtoken. We shrink that
				node.tokens.front().removeOverlapWith({});

				assert(node.tokens.front().length == (BLOCK_SIZE - spaceLeft));
			}

			for(auto iter = tokens.begin(); iter != tokens.end() && spaceLeft;)
			{
				if(iter->length <= spaceLeft)
				{
					NetworkToken tokenMoved = *iter;
					tokens.erase(iter);
					removeOverlapWithToken({tokenMoved});

#ifdef VERY_AGGRESSIVE_ASSERT
					//Validate the length of the token
					size_t tokenLength = 0;
					for(const auto & subToken : tokenMoved.sourceToken)
						tokenLength += subToken.length;
					assert(tokenLength == tokenMoved.length);
#endif

					//Merge the token to the NetworkNode
					tokenMoved.sourceBlockID = node.block;
					node.appendToken(tokenMoved);

					//Update the space
					assert(tokenMoved.length <= spaceLeft);
					assert(tokenMoved.length <= lengthToAllocate);

					spaceLeft -= tokenMoved.length;
					lengthToAllocate -= tokenMoved.length;
				}
				else
					iter += 1;
			}

			assert(lengthToAllocate == getOccupationLevel());
			spaceLeftAfterward.emplace_back(spaceLeft, counter);
		}

		//We may, due to duplicates, have too many nodes left (several huge tokens that heavily overlap but not completely)
		// If we fear we may be in this scenario, we need to run the loop once more after `removeOverlapWithToken` with the first token
		if(tokens.size() > 1)
		{
			const auto frontToken = tokens.front();

			//We don't infinitely loop. If we already triggered a reloop for the same front token, we let it go
			if(!didReloopOnce || (reloopSource != frontToken.sourceBlockID || reloopDest != frontToken.destinationBlockID))
			{
				didReloopOnce = true;
				reloopSource = frontToken.sourceBlockID;
				reloopDest = frontToken.destinationBlockID;

				//Remove the token before calling removeOverlapWithToken
				tokens.erase(tokens.begin());
				removeOverlapWithToken({frontToken});

				//Insert it back at the beginning
				tokens.insert(tokens.begin(), frontToken);

				if(tokens.size() > 1)
				{
					needReloop = true;
					spaceLeftAfterward.clear();
				}
			}
		}

	} while(needReloop);

	bool needDivide = lengthToAllocate != 0;
	if(needDivide)
	{
		assert(tokens.size() == 1);
		auto firstToken = tokens.front();

		//We keep the block we divided in superNode so the caller can figure what happened
		//We try to cut the data left in as few chunks as possible
		sort(spaceLeftAfterward.begin(), spaceLeftAfterward.end(), [](const pair<size_t, size_t> & a, const pair<size_t, size_t> & b) { return a.first > b.first; });

		for(auto & spaceLeft : spaceLeftAfterward)
		{
			const size_t lengthToCopy = MIN(spaceLeft.first, lengthToAllocate);
			NetworkToken subSequence = firstToken.extractSubsequence(lengthToCopy);
			firstToken.removeOverlapWith({subSequence});

			if(spaceLeft.second == 0)
			{
				subSequence.sourceBlockID = node1.block;
				node1.appendToken(subSequence);
			}
			else
			{
				subSequence.sourceBlockID = node2.block;
				node2.appendToken(subSequence);
			}

			lengthToAllocate -= lengthToCopy;
			if(lengthToAllocate == 0)
				break;
		}

		assert(lengthToAllocate == 0);
	}

	assert(node1.getOccupationLevel() <= BLOCK_SIZE);
	assert(node2.getOccupationLevel() <= BLOCK_SIZE);

#ifdef VERY_AGGRESSIVE_ASSERT
	for(char i = 0; i < 2; ++i)
	{
		size_t length = 0;
		for(const auto & token : (i == 0 ? node1 : node2).tokens)
			length += token.length;
		assert(length <= BLOCK_SIZE);
	}
#endif

	return needDivide;
}

DetailedBlock NetworkNode::compileLayout(bool wantWriteLayout, const VirtualMemory & memoryLayout) const
{
	DetailedBlock output(block);
	//If we are the final form, we sadly must respect blockFinalLayout, possibly at the cost of extra fragmentation
	if(wantWriteLayout && isFinal)
		output = blockFinalLayout;

	//We have to fit everything left in tokens in output
	for(const auto & netToken : tokens)
	{
		//The self reference contains all the data that belong here, which may be more than what is actually in the cache
		const bool isOversizedSelfReference = isFinal && netToken.destinationBlockID == block;
		if(wantWriteLayout && isOversizedSelfReference)
			continue;

		for (auto token : netToken.sourceToken)
		{
			//Sometimes, we may want to perform more copy than necessary (if a chunk is used in multiple blocks)
			if(!isFinal)
			{
				for(auto chunk = output.segments.cbegin(); chunk != output.segments.cend(); )
				{
					if(chunk->tagged && chunk->fitWithin(chunk->source, chunk->length, token.origin))
					{
						const size_t skip = chunk->length - (token.origin.getAddress() - chunk->source.getAddress());
						const size_t delta = MIN(skip, token.length);

						token.origin += delta;
						token.length -= delta;
						token.finalAddress += delta;

						if(token.length == 0)
							break;

						assert(token.length < BLOCK_SIZE);
						chunk = output.segments.cbegin();
					}
					else
						chunk += 1;
				}
			}

			if(token.length > 0)
			{
				//We're looking for data to load in the cache. We have to exclude what is going to be imported (no point in copying that into the cache, as we're not erasing it)
				if(!wantWriteLayout && isOversizedSelfReference)
				{
					size_t processedLength = 0;
					memoryLayout.iterateTranslatedSegments(token.origin, token.length, [&](const Address & from, const size_t length, bool) {
						if(from == block)
						{
							const bool result = output.fitSegmentInUntagged({Token(token.origin + processedLength, length, token.origin + processedLength), true});
							assert(result);
						}
						processedLength += length;
					});
				}
				else
				{
					const bool result = output.fitSegmentInUntagged({token, true});
					assert(result);
				}

			}
		}
	}

	//We make sure we're providing a valid layout
#ifdef VERY_AGGRESSIVE_ASSERT
	size_t length = 0;
	for(const auto & seg : output.segments)
		length += seg.length;
	assert(length <= BLOCK_SIZE);
#endif

	return output;
}

void Network::buildNetworkTokenArray(const vector<Block> &blocks, const vector<size_t> &network, vector<NetworkToken> &tokens) const
{
	//Size the token array
	size_t nbTokens = 0;
	for (const size_t &item : network)
		nbTokens += blocks[item].data.size();
	tokens.reserve(nbTokens);

	//Fill the array
	for (const size_t &item : network)
	{
		const Block &block = blocks[item];
		tokens.insert(tokens.end(), block.data.cbegin(), block.data.cend());
	}

	sort(tokens.begin(), tokens.end(), [](const NetworkToken & a, const NetworkToken & b) {
		if(a.sourceBlockID != b.sourceBlockID)
			return a.sourceBlockID < b.sourceBlockID;
		return a.destinationBlockID < b.destinationBlockID;
	});

	//We must now groups multiple token from the same blocks to another same block
	size_t blockBase = 0;
	BlockID currentSourceBlockID = tokens.front().sourceBlockID;
	unordered_set<BlockID> destinationBlocks;

	bool needCompact = false;
	size_t readingHead = 0, length = tokens.size();
	while (readingHead < length)
	{
		const NetworkToken &currentToken = tokens[readingHead];
		if (currentToken.sourceBlockID != currentSourceBlockID)
		{
			currentSourceBlockID = currentToken.sourceBlockID;
			destinationBlocks.clear();
			blockBase = readingHead;
		}

		//We already have a token toward this block! We may merge them
		if (!destinationBlocks.emplace(currentToken.destinationBlockID).second)
		{
			for (size_t j = blockBase; j < readingHead; ++j)
			{
				if(tokens[j].cleared)
					continue;

				if (tokens[j].destinationBlockID == currentToken.destinationBlockID)
				{
					tokens[j].length += currentToken.length;
					tokens[j].sourceToken.emplace_back(currentToken.sourceToken.front());

					tokens[readingHead].cleared = true;
					needCompact = true;
					break;
				}
			}
		}

		readingHead += 1;
	}

	if(needCompact)
	{
		//We have cleared multiple tokens. We must remove them
		tokens.erase(remove_if(tokens.begin(), tokens.end(), [](const NetworkToken & token) { return token.cleared; }),
					tokens.end());
	}
}

NetworkToken Network::findLargestToken()
{
	NetworkToken invalidToken({Address(0), 0, Address(0)});	invalidToken.cleared = true;
	NetworkToken & outputToken = invalidToken;
	int64_t maxToken = INT64_MIN;

	for(auto & node : nodes)
	{
		//If the node has no outgoing data, there is no outgoing link.
		//If the node is final, its data could be pulled whenever the receiving node turn final
		if(node.nbSourcesOut == 0 || node.isFinal)
			continue;

		//Does the need need an update?
		if(node.needRefreshLargestToken)
		{
			node.refreshLargestToken([&](const NetworkToken & token) { return computeLinkWeigth(token); });
			node.needRefreshLargestToken = false;
		}

		if(node.tokens.size() <= node.largestToken)
			continue;

		const auto & token = node.tokens[node.largestToken];
		if(token.cleared)
			continue;

		assert(token.sourceBlockID == node.block);

		//This may happen if all nodes requesting data from this node turned final, without selecting our links
		// In those cases, all links but the internal one may have been deleted
		if(token.sourceBlockID == token.destinationBlockID)
			continue;

		long bonus = 0;
		if(memoryLayout.hasCachedWrite)
		{
			//This let us cleanly
			if(memoryLayout.cachedWriteBlock == token.destinationBlockID)
				bonus = 5;
			else if(memoryLayout.cachedWriteBlock == token.sourceBlockID)
				bonus = 3;
		}

		//We need to find a way to cache that
		const auto linkWeight = node.largestTokenWeight + bonus;
		if(linkWeight > maxToken)
		{
			outputToken = token;
			maxToken = linkWeight;
		}
	}
	return outputToken;
}

enum DataSource {
	SOURCE_ORIGIN = 1,
	DESTINATION_ORIGIN,
	COMMON_ORIGIN
};

void determineBlockDataLocation(const NetworkNode & source, const NetworkNode & destination, unordered_map<BlockID, DataSource> & location)
{
	location.reserve(source.tokens.size() + destination.tokens.size());

	for(const auto & data : source.tokens)
		location.emplace(data.destinationBlockID, SOURCE_ORIGIN);

	for(const auto & data : destination.tokens)
	{
		auto &match = location[data.destinationBlockID];
		if(match == SOURCE_ORIGIN)
			match = COMMON_ORIGIN;
		else
			match = DESTINATION_ORIGIN;
	}
}

void generateChanges(const unordered_map<BlockID, DataSource> & oldLocation, const unordered_map<BlockID, DataSource> & newLocation, vector<pair<BlockID, bool>> & changesForSrc, vector<pair<BlockID, bool>> & changesForDest)
{
	for(const auto & final : newLocation)
	{
		const BlockID & block = final.first;

		auto origin = oldLocation.find(block);

		//Data doesn't exist (wtf!?) or no change
		if(origin == oldLocation.cend() || origin->second == final.second)
			continue;

		switch (final.second)
		{
			case COMMON_ORIGIN:
			{
				if(origin->second == SOURCE_ORIGIN)
					changesForDest.emplace_back(block, true);
				else
					changesForSrc.emplace_back(block, true);
				break;
			}

			case SOURCE_ORIGIN:
			{
				changesForDest.emplace_back(block, false);

				//Is a win for source
				if(origin->second != COMMON_ORIGIN)
					changesForSrc.emplace_back(block, true);
				break;
			}

			case DESTINATION_ORIGIN:
			{
				changesForSrc.emplace_back(block, false);

				//Is a win for final
				if(origin->second != COMMON_ORIGIN)
					changesForDest.emplace_back(block, true);

				break;
			}
		}
	}

	sort(changesForSrc.begin(), changesForSrc.end(), [](const pair<BlockID, bool> & a, const pair<BlockID, bool> & b) { return a < b; });
	sort(changesForDest.begin(), changesForDest.end(), [](const pair<BlockID, bool> & a, const pair<BlockID, bool> & b) { return a < b; });
}

void Network::performToken(NetworkNode & source, NetworkNode & destination, SchedulerData & schedulerData)
{
	NetworkNode fakeCommonNode = source;
	vector<NetworkToken> & tokenPool = fakeCommonNode.tokens;

	NetworkToken oldSource(vector<Token>{});
	bool hadSource = false;

	//In order to determine the length of source data already in source, we need to have a look before merger
	auto sourceCoreIter = lower_bound(tokenPool.begin(), tokenPool.end(), source.block);
	if(sourceCoreIter != tokenPool.end() && sourceCoreIter->destinationBlockID == source.block)
	{
		oldSource = *sourceCoreIter;
		tokenPool.erase(sourceCoreIter);
		hadSource = true;
	}

	//Create a super node containing both nodes
	//We can then simply dispatch the tokens between the two nodes
	fakeCommonNode.tookOverNode(destination, true);

	//We find both core tokens (that is the tokens that belong to either of the nodes)
	auto destinationCoreIter = lower_bound(tokenPool.begin(), tokenPool.end(), destination.block);
	assert(destinationCoreIter != tokenPool.end());
	assert(destinationCoreIter->destinationBlockID == destination.block);

	NetworkToken destinationCore = *destinationCoreIter;
	destinationCore.sourceBlockID = destinationCore.destinationBlockID;
	destinationCore.cleared = false;
	tokenPool.erase(destinationCoreIter);

	//Remove overlap
	fakeCommonNode.removeOverlapWithToken({destinationCore});

	//The prior iterator may be invalid after the merger
	sourceCoreIter = lower_bound(tokenPool.begin(), tokenPool.end(), source.block);
	const bool hasSourceCore = sourceCoreIter != tokenPool.end() && sourceCoreIter->destinationBlockID == source.block;
	const size_t sourceCoreLength = hasSourceCore ? sourceCoreIter->length : 0;

	//Create the new containers
	NetworkNode newSource(source.block, hadSource ? vector<NetworkToken>{oldSource} : vector<NetworkToken>());
	NetworkNode newDest(destination.block, {destinationCore});
	assert(!newDest.tokens.empty());

	if(hasSourceCore)
	{
		//We have a back reference from the destination to the source.
		//We may or may not apply it, depending of how much it overlap with the data in the destination
		//If the overlap is higher than the common margin, we will have to leave this token in the pool, maybe to be left in the destination
		const size_t totalOccupationLevel = newSource.getOccupationLevel() + newDest.getOccupationLevel() + fakeCommonNode.getOccupationLevel();
		const size_t freeSpace = 2 * BLOCK_SIZE - totalOccupationLevel;
		if(sourceCoreIter->overlapWith(newDest.tokens.front()) <= freeSpace)
		{
			if(newSource.tokens.empty())
			{
				newSource.tokens.emplace_back(*sourceCoreIter);
			}
			else
			{
				auto & token = newSource.tokens.front();
				token.length += sourceCoreIter->length;
				token.sourceToken.insert(token.sourceToken.end(), sourceCoreIter->sourceToken.begin(), sourceCoreIter->sourceToken.end());
			}

			tokenPool.erase(sourceCoreIter);
		}
	}

	//We don't remove overlap with fakeNode yet, as we may pull more data with final

	newSource.blockFinalLayout = source.blockFinalLayout;
	newSource.lengthFinalLayout = source.lengthFinalLayout;

	newDest.blockFinalLayout = destination.blockFinalLayout;
	newDest.lengthFinalLayout = destination.lengthFinalLayout;

	/// We want to determine whether we have enough headroom to make one of the blocks final
	size_t sourceLength = sourceCoreLength;
	size_t destLength = destinationCore.length;
	size_t lengthToAllocateLeft = fakeCommonNode.getOccupationLevel();

	//We can finish source AND destination, and still store the data that is necessary elsewhere
	if(source.lengthFinalLayout + destination.lengthFinalLayout + lengthToAllocateLeft < 2 * BLOCK_SIZE)
	{
		newSource.setFinal(source.lengthFinalLayout, memoryLayout);
		newDest.setFinal(destination.lengthFinalLayout, memoryLayout);
	}

	//We can finish destination
	else if(sourceLength + destination.lengthFinalLayout + lengthToAllocateLeft < 2 * BLOCK_SIZE)
	{
		newDest.setFinal(destination.lengthFinalLayout, memoryLayout);
	}

	//We can finish source
	else if(source.lengthFinalLayout + destLength + lengthToAllocateLeft < 2 * BLOCK_SIZE)
	{
		newSource.setFinal(source.lengthFinalLayout, memoryLayout);
	}

	//If one of the node have turned final, we remove incomming data from any other potential token
	if(newSource.isFinal || newDest.isFinal)
	{
		vector<NetworkToken> finalToken;
		//Source is going to remove anything that might end up in the self reference token. No need to care about what is actually already there
		if(newSource.isFinal)
		{
			assert(newSource.tokens.size() == 1);

			NetworkToken fakeSourceToken = newSource.tokens.front();
			fakeSourceToken.sourceToken.clear();
			fakeSourceToken.sourceToken.reserve(newSource.blockFinalLayout.segments.size());
			
			for(const auto & finalData : newSource.blockFinalLayout.segments)
			{
				if(finalData.tagged)
					fakeSourceToken.sourceToken.emplace_back(Token(finalData.destination, finalData.length, finalData.source));
			}
			
			finalToken.emplace_back(fakeSourceToken);
		}
		
		if(newDest.isFinal)
		{
			assert(newDest.tokens.size() == 1);
			
			NetworkToken fakeDestToken = newDest.tokens.front();
			fakeDestToken.sourceToken.clear();
			fakeDestToken.sourceToken.reserve(newDest.blockFinalLayout.segments.size());
			
			for(const auto & finalData : newDest.blockFinalLayout.segments)
			{
				if(finalData.tagged)
					fakeDestToken.sourceToken.emplace_back(Token(finalData.destination, finalData.length, finalData.source));
			}
			
			finalToken.emplace_back(fakeDestToken);
		}
		
		fakeCommonNode.removeOverlapWithToken(finalToken);
	}
	else if(!newSource.tokens.empty())
		fakeCommonNode.removeOverlapWithToken({newSource.tokens.front()});

	///At this point, if one of the blocks was going to be finished, we already allocated the relevant memory
	///	We can then dispatch the data left

	fakeCommonNode.dispatchInNodes(newSource, newDest);
	Scheduler::networkSwapCodeGeneration(newSource, newDest, memoryLayout, schedulerData);

	// We need to determine who lost and who won. The details don't really matter
	//First, we make an inventory before and after
	unordered_map<BlockID, DataSource> dataOrigin, dataFinal;
	determineBlockDataLocation(source, destination, dataOrigin);
	determineBlockDataLocation(newSource, newDest, dataFinal);

	//Before updating the tokens, we signal potential final moves
	if(newSource.isFinal)	pulledEverythingForNode(source);
	if(newDest.isFinal)		pulledEverythingForNode(destination);

	//Update the network
	source.tokens = newSource.tokens;			source.refreshOutgoingData();
	destination.tokens = newDest.tokens;		destination.refreshOutgoingData();

	//We then allocate the buffer to receive this data
	vector<pair<BlockID, bool>> changesForSrc, changesForDest;
	changesForSrc.reserve(newSource.tokens.size());
	changesForDest.reserve(newDest.tokens.size());

	generateChanges(dataOrigin, dataFinal, changesForSrc, changesForDest);
	nodesPartiallySwapped(changesForSrc, changesForDest);
}

bool netNeedHalfSwap(const NetworkNode & source, const NetworkNode & destination)
{
	if(source.nbSourcesOut == 1 && destination.nbSourcesOut == 0)
		return false;

	if(source.nbSourcesOut == 1 && destination.nbSourcesOut == 1)
	{
		for(const auto & token : destination.tokens)
		{
			if(token.cleared || token.destinationBlockID == destination.block)
				continue;

			//If the only outgoing link is toward source, we're golden!
			if(token.destinationBlockID == source.block)
				return false;
		}
	}

	return true;
}

bool Network::performBestSwap(SchedulerData & schedulerData)
{
	NetworkToken bestToken = findLargestToken();

	//That means we're done
	if(bestToken.cleared)
	{
		memoryLayout.commitCachedWrite(schedulerData);
		return false;
	}

#ifdef VERY_AGGRESSIVE_ASSERT
	for(auto & node : nodes)
	{
		if(!node.isFinal && node.nbSourcesOut > 0)
		{
			auto & token = node.tokens[node.largestToken];
			assert(token.destinationBlockID != token.sourceBlockID);
		}
	}
#endif

	//Those links are explicitely forbidden, as pullDataToNode take care of nodes whose the only link left is this one
	assert(bestToken.sourceBlockID != bestToken.destinationBlockID);

	NetworkNode & source = findNodeWithBlock(bestToken.sourceBlockID);
	NetworkNode & destination = findNodeWithBlock(bestToken.destinationBlockID);

	assert(!destination.isFinal);

#ifdef PRINT_SELECTED_LINKS
	cout << "[DEBUG]: Processing token from 0x" << hex << bestToken.sourceBlockID.value << " (pass 0x" << source.touchCount++ <<") to 0x" << bestToken.destinationBlockID.value << " (pass 0x" << destination.touchCount++ <<") with 0x" << bestToken.length << " bytes of data" << dec << endl;
#endif

	source.tokens[source.largestToken].cleared = true;

	bool ignoreRefresh = false;
	if(bestToken.length >= destination.sumOut)
	{
		//We can perform a single swap (or hswap) that will let destination become its final form and the source node get the extra data
		if(netNeedHalfSwap(source, destination))
		{
			//The source will need to provide data to more blocks, we simply clear the destination with a hswap
			Scheduler::halfSwapCodeGeneration(source, destination, memoryLayout, schedulerData);
			nodeSiphonned(source.block, source.tookOverNode(destination));
			pulledEverythingForNode(destination);
			destination.isFinal = true;
		}
		else
		{
			bool sourceWasFinal = source.isFinal;

			//Source also can reach its final form!
			Scheduler::reorderingSwapCodeGeneration(source, destination, memoryLayout, schedulerData);
			pulledEverythingForNode(destination);

			if(!sourceWasFinal)
				pulledEverythingForNode(source);

			source.refreshOutgoingData();
			ignoreRefresh = true;
		}

 		destination.refreshOutgoingData();
	}
	else
	{
		//Alright, we need to pick what to swap
		//The ultimate goal is to always get nbSourcesOut down
		//In order to do that, we create a virtual node grouping the two nodes, merging each outgoing data stream
		//Then, we allocate those new super-token in each real node, after first allocating the data of the node
		//We may have to split one token in order to pad both nodes in case of high occupation
		//Although the problem is similar to the knapsack problem (NP complete), we can easily solve this one by vertue of
		// 		knowing a solution is possible (the data come from only two nodes) and being allowed to split on block to pad

		performToken(source, destination, schedulerData);
	}

	//Unless when _really_ irrelevant, we selectively refresh nodes
	if(!ignoreRefresh)
	{
		source.refreshLargestToken([&](const NetworkToken & token) { return computeLinkWeigth(token); });
		destination.refreshLargestToken([&](const NetworkToken & token) { return computeLinkWeigth(token); });

		//Refresh largest links if they impacted the links we updated
		for(auto & node : nodes)
		{
			if(!node.isFinal && !node.needRefreshLargestToken && node.block != source.block && node.block != destination.block)
			{
				//Mark the node as asking for an update
				auto & token = node.tokens[node.largestToken];
				if(token.cleared || token.destinationBlockID == source.block || token.destinationBlockID == destination.block)
				{
					node.needRefreshLargestToken = true;
				}

#ifdef VERY_AGGRESSIVE_ASSERT
				else if(token.destinationBlockID == token.sourceBlockID && node.tokens.size() > 1)
					assert(token.destinationBlockID != token.sourceBlockID);
#endif
			}
		}
	}

	return true;
}

void Network::pulledEverythingForNode(NetworkNode & node)
{
	if(!node.tokens.empty() || node.nbSourcesIn)
	{
		//Token is sorted
		for(auto & networkNode : nodes)
		{
			if(networkNode.isFinal)
				continue;
			
			size_t tokenPos = 0;
			//Clear all token toward us
			for(auto & token : networkNode.tokens)
			{
				if(!token.cleared && token.destinationBlockID == node.block)
				{
					//We may have a token due to an internal transfer
					if(!networkNode.isFinal && networkNode.block != node.block)
					{
						assert(networkNode.nbSourcesOut != 0);
						networkNode.sumOut -= token.length;
						networkNode.nbSourcesOut -= 1;
						networkNode.tokens.erase(networkNode.tokens.begin() + tokenPos);

						//We may be interfering with largestToken.
						if(tokenPos < networkNode.largestToken)
							networkNode.largestToken -= 1;
						else if(tokenPos == networkNode.largestToken)
							networkNode.needRefreshLargestToken = true;
					}
					else
					{
						token.cleared = true;
					}

					break;
				}

				tokenPos += 1;
			}
		}
	}

	node.isFinal = true;
	node.nbSourcesIn = 0;
}

void Network::nodeSiphonned(const BlockID & destination, const vector<BlockID> & blocksWithLessSources)
{
	if(blocksWithLessSources.empty())
		return;

	auto startIter = blocksWithLessSources.cbegin();

	for(auto & networkNode : nodes)
	{
		if(networkNode.block == *startIter)
		{
			//Due to overlap, some networkToken may be deleted and are thus not explicitly accounted for
			// We won't decrement nbSourcesOut if we can't find the token
			for(const auto & token : networkNode.tokens)
			{
				if(token.destinationBlockID == destination)
				{
					assert(networkNode.nbSourcesOut != 0);
					networkNode.nbSourcesOut -= 1;
					break;
				}
			}

			if(++startIter == blocksWithLessSources.cend())
				break;
		}
	}
}

//bool = true mean a win, false is a loss
void Network::nodesPartiallySwapped(const vector<pair<BlockID, bool>> & changesForA, const vector<pair<BlockID, bool>> & changesForB)
{
	if(changesForA.empty() && changesForB.empty())
		return;

	auto aChangesIter = changesForA.cbegin();
	auto bChangesIter = changesForB.cbegin();

	for(auto & networkNode : nodes)
	{
		if(networkNode.isFinal)
			continue;

		bool bothAtEnd = true, aMatch = false, bMatch = false;

		if(aChangesIter != changesForA.cend())
		{
			aMatch = networkNode.block == aChangesIter->first;
			bothAtEnd = false;
		}

		if(bChangesIter != changesForB.cend())
		{
			bMatch = networkNode.block == bChangesIter->first;
			bothAtEnd = false;
		}

		if(bothAtEnd)
			break;

		//Both nodes reference the current one, this can only mean an exchange (overwise, one would shrink but not win/loose)
		if(aMatch && bMatch)
		{
			aChangesIter += 1;
			bChangesIter += 1;
		}

		else if(aMatch)
			networkNode.nbSourcesIn += aChangesIter++->second ? 1 : -1;

		else if(bMatch)
			networkNode.nbSourcesIn += bChangesIter++->second ? 1 : -1;
	}
}

void Network::performFinalFlush(SchedulerData & schedulerData)
{
	/*
	 * We may have nodes loosely connected to the network that may be optimized out.
	 * Those nodes need to provide data to the network but only pull data from outside it. Those external relations are ignored.
	 * They are easy to solve but need attention and won't be detected by the main loop
	 */
	for(auto & node : nodes)
	{
		if(!node.isFinal)
		{
#ifdef PRINT_SELECTED_LINKS
			cout << hex << "[DEBUG]: Pulling node 0x" << node.block.value << dec << endl;
#endif
			node.isFinal = true;
			Scheduler::pullDataToNode(node, memoryLayout, schedulerData);
			node.nbSourcesIn = 0;
		}
	}
}
