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

#ifndef HERMES_TOKEN_H
#define HERMES_TOKEN_H

#include <sys/param.h>
#include <algorithm>

struct Token
{
	Address finalAddress;
	size_t length;
	Address origin;

	Token(const Address & _finalAddress, const size_t & _length, const Address & _origin)
			: finalAddress(_finalAddress), length(_length), origin(_origin) {}

	static size_t getLengthLeftInBlock(const Address address)
	{
		return BLOCK_SIZE - address.getOffset();
	}

	static void insertToken(BSDiffMoves input, vector<Token> & output)
	{
		Address inputStart(input.start), inputDest(input.dest);
		Address finalAddress = inputStart + input.length;

		while(inputStart < finalAddress)
		{
			const size_t lengthLeftInBlockDest = getLengthLeftInBlock(inputDest);
			const size_t lengthLeftInBlockOrig = getLengthLeftInBlock(inputStart);
			const size_t lengthLeftInBlock = MIN(lengthLeftInBlockDest, lengthLeftInBlockOrig);

			output.emplace_back(inputDest, MIN(lengthLeftInBlock, input.length), inputStart);

			input.length -= lengthLeftInBlock;
			inputStart += lengthLeftInBlock;
			inputDest += lengthLeftInBlock;
		}
	}

	Token getReverse() const
	{
		return Token(origin, length, finalAddress);
	}
};

//Network tokens are currently used to group packets of data based on their destination block
struct NetworkToken
{
	bool cleared;
	vector<Token> sourceToken;
	BlockID sourceBlockID;
	BlockID destinationBlockID;
	size_t length;

	NetworkToken(const Token & token) : sourceToken({token}), sourceBlockID(token.origin), destinationBlockID(token.finalAddress), length(token.length), cleared(false) {}
	NetworkToken(const vector<Token> & tokens) : sourceToken(tokens), sourceBlockID(tokens.empty() ? Address(0) : tokens.front().origin), destinationBlockID(tokens.empty() ? Address(0) : tokens.front().finalAddress), length(0), cleared(false)
	{
		for(const auto &token : sourceToken)
		{
			length += token.length;
		}
	}

	vector<Token> extractSubsequence(size_t length)
	{
		vector<Token> output;

		sort(sourceToken.begin(), sourceToken.end(), [](const Token &a, const Token &b) {
			return a.length > b.length;
		});

		//Get as many full chunks as possible
		for(auto iter = sourceToken.begin(); length != 0 && iter != sourceToken.end();)
		{
			if(iter->length < length)
			{
				output.push_back(*iter);
				length -= iter->length;
				this->length -= iter->length;

				const auto offset = iter - sourceToken.begin();
				sourceToken.erase(iter);
				iter = sourceToken.begin() + offset;
			}
			else
				iter++;
		}

		if(length && sourceToken.size() > 0)
		{
			Token & largestToken = sourceToken.front();
			output.push_back({largestToken.finalAddress, length, largestToken.origin});

			largestToken.finalAddress += length;
			largestToken.origin += length;
			largestToken.length -= length;
			this->length -= length;
		}

		return output;
	}

	vector<Token> getTokenSortedByOrigin() const
	{
		vector<Token> output(sourceToken);

		if(output.size() > 1)
		{
			//We check if we actually need to sort the array
			for(auto iter = output.cbegin(), endArray = output.cend() - 1; iter != endArray; ++iter)
			{
				//The array is unsorted
				if(iter->origin > (iter + 1)->origin)
				{
					sort(output.begin(), output.end(), [](const Token & a, const Token & b) { return a.origin < b.origin; });
					break;
				}
			}
		}

		return output;
	}

	size_t removeOverlapWith(const vector<NetworkToken> & networkToken);
	size_t overlapWith(const NetworkToken & networkToken) const;

	bool operator<(const NetworkToken & b) const { return destinationBlockID < b.destinationBlockID; }
	bool operator<(const BlockID & b) const { return destinationBlockID < b; }
};
#endif //HERMES_TOKEN_H
