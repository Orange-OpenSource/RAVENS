//
// Created by Emile-Hugo Spir on 3/21/18.
//

#ifndef HERMES_NETWORK_H
#define HERMES_NETWORK_H

#include <unordered_set>
#include <unordered_map>

struct NetworkNode
{
	BlockID block;
	DetailedBlock blockFinalLayout;
	vector<NetworkToken> tokens;	//The data we currently hold
	size_t nbSourcesIn;
	size_t nbSourcesOut;
	size_t sumOut;
	size_t lengthFinalLayout;

#ifdef PRINT_SELECTED_LINKS
	size_t touchCount;
#endif

	size_t largestToken;
	bool isFinal;
	bool needRefreshLargestToken;

	NetworkNode(const BlockID & currentBlock, const vector<NetworkToken> & allTokens) : block(currentBlock), sumOut(0), nbSourcesIn(0), nbSourcesOut(0), lengthFinalLayout(0), blockFinalLayout(currentBlock), largestToken(0), needRefreshLargestToken(false), isFinal(false)
#ifdef PRINT_SELECTED_LINKS
	,touchCount(0)
#endif
	{
		for(const auto & token : allTokens)
			appendToken(token);

#ifdef VERY_AGGRESSIVE_ASSERT
		assert(getOccupationLevel() <= BLOCK_SIZE);
#endif
	}

	vector<BlockID> tookOverNode(const NetworkNode & pulledNode, bool bypassBlockIDDrop = false);

	void refreshLargestToken(function<size_t(const NetworkToken&)> lambda)
	{
		if(nbSourcesOut != 0)
		{
			size_t counter = 0;
			int64_t maxLength = INT64_MIN;
			for(const auto & token : tokens)
			{
				if(token.destinationBlockID != block)
				{
					const int64_t tokenWeight = lambda(token);
					if(tokenWeight > maxLength)
					{
						maxLength = tokenWeight;
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

			//buildNetworkTokenArray merge any duplicates
			for(auto & existingToken : tokens)
				assert(existingToken.destinationBlockID != token.destinationBlockID);

			tokens.insert(lower_bound(tokens.begin(), tokens.end(), token), token);
		}
		else if(token.destinationBlockID == block)
		{
			nbSourcesIn += 1;
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

		size_t output = 0;

		for(const auto & token : tokens)
		{
			//When the node is isFinal, we may have a dummy token.
			//It takes up the space required by the final data but doesn't contain any token, in order not to interfere with the cache loading
			//We have to add the data manually
			if(isFinal && token.destinationBlockID == block)
			{
				output += lengthFinalLayout;
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

	void setFinal(size_t finalLength)
	{
		isFinal = true;
		nbSourcesIn = 0;

		bool foundCore = false;
		NetworkToken dummyToken = NetworkToken(Token(Address(block, 0), finalLength, Address(block, 0)));
		NetworkToken & selfToken = dummyToken;

		//Find the token containing our data
		for(auto & token : tokens)
		{
			if(token.destinationBlockID == block)
			{
				selfToken = token;
				selfToken.length = finalLength;
				foundCore = true;
				break;
			}
		}

		//If no real token, we remove the dummy placeholder
		if(!foundCore)
			selfToken.sourceToken.clear();

		//Write the final tokens to the self token, from our final layout
		for(const auto & token : blockFinalLayout.segments)
		{
			if(token.tagged && token.source == block)
			{
				bool tokenAlreadyThere = false;
				for(const Token & existingToken : selfToken.sourceToken)
				{
					if(existingToken.finalAddress == token.destination)
					{
						tokenAlreadyThere = true;
						break;
					}
				}

				//Insert the token if the data isn't already there
				if(!tokenAlreadyThere)
					selfToken.sourceToken.emplace_back(Token(token.destination, token.length, token.source));
			}
		}

		//If a new token, we insert it into the array
		if(!foundCore)
			tokens.emplace_back(dummyToken);
	}

	DetailedBlock compileLayout(bool wantWriteLayout) const;
};

class Network
{
	vector<NetworkNode> nodes;
	VirtualMemory memoryLayout;
	unordered_map<BlockID, size_t> nodeIndex;

	void buildNetworkTokenArray(const vector<Block> &blocks, const vector<size_t> &network, vector<NetworkToken> &tokens) const;
	void performToken(NetworkNode & source, NetworkNode & destination, SchedulerData & schedulerData);

	//The bool being true mean the node got data from an additionnal block. false means it lost
	//We assume both vectors were sorted
	void nodesPartiallySwapped(const vector<pair<BlockID, bool>> & changesForA, const vector<pair<BlockID, bool>> & changesForB);
	void nodeSiphonned(const BlockID & destination, const vector<BlockID> & blocksWithLessSources);
	void pulledEverythingForNode(NetworkNode & node);
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
			nodes.emplace_back(blocks[blockIndex].blockID, tokens);
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
