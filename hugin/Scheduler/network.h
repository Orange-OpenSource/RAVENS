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

#ifndef HERMES_NETWORK_H
#define HERMES_NETWORK_H

#include <functional>
#include <unordered_set>
#include <unordered_map>

struct NetworkNode
{
	BlockID block;
	DetailedBlock blockFinalLayout;
	vector<NetworkToken> tokens;	//The data we currently hold
	size_t nbSourcesOut;
	size_t sumOut;
	size_t lengthFinalLayout;

#ifdef PRINT_SELECTED_LINKS
	size_t touchCount;
#endif

	size_t largestToken;
	int64_t largestTokenWeight;
	bool isFinal;
	bool needRefreshLargestToken;

	NetworkNode(const BlockID & curBlockID, const vector<NetworkToken> & allTokens) : block(curBlockID), blockFinalLayout(curBlockID),
				nbSourcesOut(0), sumOut(0), lengthFinalLayout(0),
#ifdef PRINT_SELECTED_LINKS
				touchCount(0),
#endif
				largestToken(0), isFinal(false), needRefreshLargestToken(false)
	{
		//Very expensive,
		for(const auto & token : allTokens)
			appendToken(token);

#ifdef VERY_AGGRESSIVE_ASSERT
		assert(getOccupationLevel() <= BLOCK_SIZE);
#endif
	}

	NetworkNode(const Block & curBlock, const vector<NetworkToken> & allTokens) : block(curBlock.blockID), blockFinalLayout(curBlock.blockID),
				nbSourcesOut(0), sumOut(0), lengthFinalLayout(0),
#ifdef PRINT_SELECTED_LINKS
				touchCount(0),
#endif
				largestToken(0), isFinal(false), needRefreshLargestToken(false)
	{
		//We first import sourceID matches, allTokens are sorted by sourceID
		auto startSourceID = lower_bound(allTokens.cbegin(), allTokens.cend(), block, [](const NetworkToken & a, const BlockID & b) { return a.sourceBlockID < b; });
		while(startSourceID != allTokens.cend() && startSourceID->sourceBlockID == block)
		{
			appendToken(*startSourceID);
			startSourceID += 1;
		}

		//We're then interested in NetworkToken for which we are destinationID.
		//	In order to detect them, we look at our Block and search for NetworkToken coming from blocks in our curBlock.blocksWithDataForCurrent
		for(const auto & link : curBlock.blocksWithDataForCurrent)
		{
			if(link.block == block)
				continue;

			auto startDestID = lower_bound(allTokens.cbegin(), allTokens.cend(), link.block, [](const NetworkToken & a, const BlockID & b) { return a.sourceBlockID < b; });
			while(startDestID != allTokens.cend() && startDestID->sourceBlockID == link.block)
			{
				//Found our match!
				if(startDestID->destinationBlockID == block)
					appendToken(*startDestID);

				startDestID += 1;
			}

		}

#ifdef VERY_AGGRESSIVE_ASSERT
		assert(getOccupationLevel() <= BLOCK_SIZE);
#endif
	}

	void tookOverNode(const NetworkNode & pulledNode, bool bypassBlockIDDrop = false);

	void refreshLargestToken(function<size_t(const NetworkToken&)> lambda)
	{
		if(!isFinal && nbSourcesOut != 0)
		{
			size_t counter = 0;
			largestTokenWeight = INT64_MIN;
			for(const auto & token : tokens)
			{
				if(token.destinationBlockID != block)
				{
					const int64_t tokenWeight = lambda(token);
					if(tokenWeight > largestTokenWeight)
					{
						largestTokenWeight = tokenWeight;
						largestToken = counter;
					}
				}

				counter++;
			}
		}
		else
			largestToken = 0;
	}

	void refreshOutgoingData()
	{
		nbSourcesOut = 0;
		sumOut = 0;

		for(const auto & token : tokens)
		{
			if(token.destinationBlockID != block && !token.cleared)
			{
				nbSourcesOut += 1;
				sumOut += token.length;
			}
		}
	}

	void appendToken(const NetworkToken & token)
	{
		if(token.sourceBlockID == block)
		{
			if(token.destinationBlockID != block)
			{
				nbSourcesOut += 1;
				sumOut += token.length;
			}
			else
				appendFinalToken(token);

			//buildNetworkTokenArray merge any duplicates, unless dispatchInNodes split then unsplit a token
			bool foundExisting = false;
			for(auto & existingToken : tokens)
			{
				if(existingToken.destinationBlockID == token.destinationBlockID)
				{
					//Remove overlap, then merge the tokens
					existingToken.removeOverlapWith({token});
					existingToken.sourceToken.insert(existingToken.sourceToken.end(), token.sourceToken.begin(), token.sourceToken.end());
					existingToken.length += token.length;

					foundExisting = true;
					break;
				}
			}

			if(!foundExisting)
				tokens.insert(lower_bound(tokens.begin(), tokens.end(), token), token);
		}
		else if(token.destinationBlockID == block)
		{
			appendFinalToken(token);
		}
	}

	void appendFinalToken(const NetworkToken & token)
	{
		if(token.destinationBlockID == block)
		{
			lengthFinalLayout += token.length;
			for(const auto & subToken : token.sourceToken)
				blockFinalLayout.insertNewSegment(subToken);

#ifdef VERY_AGGRESSIVE_ASSERT
			if(!token.sourceToken.empty())
			{
				size_t length = 0;
				for(const auto & subToken : token.sourceToken)
					length += subToken.length;
				assert(token.length == length);
			}
#endif
		}
	}

	size_t getOccupationLevel() const
	{
		DetailedBlock layout;

		size_t output = isFinal ? lengthFinalLayout : 0;

		for(const auto & token : tokens)
		{
			//When the node is isFinal, we may have a dummy token.
			//It takes up the space required by the final data but doesn't contain any token, in order not to interfere with the cache loading
			//We have to add the data manually
			if(isFinal && token.destinationBlockID == block)
			{
				//We already incremented output
				for(const auto & item : blockFinalLayout.segments)
				{
					if(item.tagged)
						layout.insertNewSegment(DetailedBlockMetadata(item.destination, item.source, item.length, false));
				}
			}
			else
			{
				for(const auto & subToken : token.sourceToken)
					layout.insertNewSegment(subToken.getReverse());
			}
		}

		for(const auto & item : layout.segments)
		{
			if(item.tagged)
				output += item.length;
		}

		return output;
	}

	size_t getTokenSum() const
	{
		size_t output = 0;

		for(const auto & token : tokens)
			output += token.length;

		return output;
	}

	size_t getOverlapLevel() const
	{
		return getTokenSum() - getOccupationLevel();
	}

	//Because we only use this method on fakeNode, we don't have to update sumOut
	void removeOverlapWithToken(const vector<NetworkToken> & extToken)
	{
		vector<size_t> indexToRemove;
		size_t counter = 0;
		for(auto &token : tokens)
		{
			token.removeOverlapWith(extToken);
			if(token.length == 0)
				indexToRemove.emplace_back(counter);

			counter += 1;
		}

		//Remove empty token
		for(auto iter = indexToRemove.crbegin(); iter != indexToRemove.crend(); ++iter)
		{
			tokens.erase(tokens.begin() + *iter);
			nbSourcesOut -= 1;
		}
	}

	bool dispatchInNodes(NetworkNode & node1, NetworkNode & node2);

	void setFinal(size_t finalLength, const BlockID & neighbour, const VirtualMemory & memoryLayout)
	{
		isFinal = true;

		bool foundCore = false;
		NetworkToken dummyToken = NetworkToken(Token(Address(block, 0), finalLength, Address(block, 0)));
		auto selfToken = ref(dummyToken);

		//Find the token containing our data
		for(auto & token : tokens)
		{
			if(token.destinationBlockID == block)
			{
				selfToken = ref(token);
				selfToken.get().length = finalLength;
				foundCore = true;
				break;
			}
		}

		//If no real token, we remove the dummy placeholder
		if(!foundCore)
			selfToken.get().sourceToken.clear();

		//Write the final tokens to the self token, from our final layout
		for(const auto & token : blockFinalLayout.segments)
		{
			if(token.tagged)
			{
				size_t tokenOffset = 0;
				//We may have final token that migrated in us while we were not ready to turn final.
				//In this case, we need to add a token so that we can protect against duplicates
				//We may also need to steal data that was duplicated and marked as for someone else from the block we're being processed with
				memoryLayout.iterateTranslatedSegments(token.source, token.length, [&](const Address & source, const size_t length)
				{
					if(source == block || source == neighbour)
					{
						//Insert the token if the data may need backup. This could cause duplicates with data already inserted in the token but removeInternalOverlap will fix that for us
						selfToken.get().sourceToken.emplace_back(Token(token.destination + tokenOffset, length, token.source + tokenOffset));
					}
					tokenOffset += length;
				});
			}
		}
		
		selfToken.get().removeInternalOverlap();
		
#ifdef VERY_AGGRESSIVE_ASSERT
		{
			//We may have inserted some duplicates but nothing should be left after removeInternalOverlap
			size_t tokenLength = 0;
			for(const auto & token : selfToken.get().sourceToken)
				tokenLength += token.length;

			assert(tokenLength <= lengthFinalLayout);
		}
#endif

		//If a new token, we insert it into the array
		if(!foundCore)
			tokens.emplace_back(dummyToken);
	}

	DetailedBlock compileLayout() const;
};

class Network
{
	vector<NetworkNode> nodes;
	VirtualMemory memoryLayout;
	unordered_map<BlockID, size_t> nodeIndex;

	void buildNetworkTokenArray(const vector<Block> &blocks, const vector<size_t> &network, vector<NetworkToken> &tokens) const;
	void performToken(NetworkNode & source, NetworkNode & destination, SchedulerData & schedulerData);

	void sourcesForFinal(const NetworkNode & node, vector<BlockID> & sources);
	void pulledEverythingForNode(NetworkNode & node, const vector<BlockID> & nodeSources);
	NetworkToken findLargestToken();

	NetworkNode & findNodeWithBlock(const BlockID & block)
	{
		return nodes[nodeIndex[block]];
	}

public:
	Network(const vector<Block> & blocks, const vector<size_t> & network) : memoryLayout(blocks, network)
	{
		vector<NetworkToken> tokens;
		buildNetworkTokenArray(blocks, network, tokens);

		nodes.reserve(network.size());
		nodeIndex.reserve(network.size());

		size_t counter = 0;
		for(const size_t & blockIndex : network)
		{
			nodes.emplace_back(blocks[blockIndex], tokens);
			nodeIndex.emplace(blocks[blockIndex].blockID, counter++);
		}

		for(auto & node : nodes)
			node.refreshLargestToken([&](const NetworkToken & token) { return computeLinkWeigth(token); });
	}

	bool performBestSwap(SchedulerData & schedulerData);

	int64_t computeLinkWeigth(const NetworkToken & token) const
	{
		size_t backLinkWeight = 0;

		auto index = nodeIndex.find(token.destinationBlockID);
		if(index != nodeIndex.end())
		{
			const NetworkNode & dest = nodes[index->second];
			if(dest.nbSourcesOut > 0)
			{
				for(const auto & destToken : dest.tokens)
				{
					if(!destToken.cleared && destToken.destinationBlockID == token.sourceBlockID)
					{
						backLinkWeight = destToken.length;
						break;
					}
				}
			}
		}

		return 3 * MIN(backLinkWeight, token.length) - 2 * abs((long long int) (backLinkWeight - token.length));
	}

	void performFinalFlush(SchedulerData & schedulerData);
};

#endif //HERMES_NETWORK_H
