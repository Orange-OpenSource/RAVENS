//
// Created by Emile-Hugo Spir on 3/14/18.
//

#ifndef HERMES_SCHEDULER_H
#define HERMES_SCHEDULER_H

#include <cstdint>
#include <vector>
#include <cassert>
#include <map>
#include <iostream>

#ifndef MIN
	#define MIN(x, y) (((x)<(y)) ? (x) : (y))
#endif
#ifndef MAX
	#define MAX(x, y) (((x)>(y)) ? (x) : (y))
#endif

#include "config.h"

using namespace std;

#include "public_command.h"

#define TMP_BUF Address(CACHE_ADDRESS)

#include "Address.h"
#include "Encoding/encoder.h"

uint8_t numberOfBitsNecessary(size_t x);
uint64_t largestPossibleValue(uint8_t numberOfBits);

struct Command
{
	INSTR command;
	BlockID mainBlock;
	size_t mainBlockOffset;

	size_t length;

	BlockID secondaryBlock;
	size_t secondaryOffset;

	size_t transactionID;

	Command(INSTR _command) : command(_command), mainBlock(0), mainBlockOffset(0), length(0), secondaryBlock(0), secondaryOffset(0), transactionID(0)
	{
		assert(command == RELEASE_BLOCK);
	}

	Command(INSTR _command, const BlockID &coreBlock) : command(_command), mainBlock(coreBlock), mainBlockOffset(0), length(0), secondaryBlock(0), secondaryOffset(0), transactionID(0)
	{
		assert(command == ERASE || command == COMMIT || command == LOAD_AND_FLUSH || command == USE_BLOCK);
	}

	Command(INSTR _command, const BlockID &coreBlock, size_t length) : command(_command), mainBlock(coreBlock), mainBlockOffset(0), length(length), secondaryBlock(0), secondaryOffset(0), transactionID(0)
	{
		assert(command == FLUSH_AND_PARTIAL_COMMIT || command == REBASE);
	}

	Command(INSTR _command, const BlockID &coreBlock, size_t coreBlockOffset, size_t length, const BlockID &secBlock, size_t secBlockOffset)
			: command(_command), mainBlock(coreBlock), mainBlockOffset(coreBlockOffset),  length(length), secondaryBlock(secBlock), secondaryOffset(secBlockOffset), transactionID(0)
	{
		assert(command == COPY);
	}

	Command(INSTR _command, const BlockID &coreBlock, size_t coreBlockOffset, size_t length)
		: command(_command), mainBlock(coreBlock), mainBlockOffset(coreBlockOffset),  length(length), secondaryBlock(0), secondaryOffset(0), transactionID(0)
	{
		assert(command == CHAINED_COPY);
	}

	Command(INSTR _command, Address originAddress, size_t length, Address destAddress) :
			Command(_command, originAddress.getBlock(), originAddress.getOffset(), length, destAddress.getBlock(), destAddress.getOffset()) {}

	Command(INSTR _command, const BlockID &coreBlock, size_t coreBlockOffset, size_t length, Address secBlock) :
			Command(_command, coreBlock, coreBlockOffset, length, secBlock, secBlock.getOffset()) {}

	Command(INSTR _command, const Address &coreBlock, size_t coreBlockOffset, size_t length, const Address &secBlock, size_t secBlockOffset)
			: Command(_command, coreBlock.getBlock(), coreBlock.getOffset() + coreBlockOffset, length, secBlock.getBlock(), secBlock.getOffset() + secBlockOffset) {}

	Command(INSTR _command, const Address &coreBlock, size_t coreBlockOffset, size_t length, const BlockID &secBlock)
			: Command(_command, coreBlock.getBlock(), coreBlock.getOffset() + coreBlockOffset, length, secBlock, 0) {}

	Command(INSTR _command, const BlockID &coreBlock, size_t coreBlockOffset, size_t length, const BlockID &secBlock)
			: Command(_command, coreBlock, coreBlockOffset, length, secBlock, 0) {}

	Command(const PublicCommand & pub) : command(pub.command), mainBlock(pub.mainAddress),
										 length(pub.length), secondaryBlock(pub.secondaryAddress),
										 mainBlockOffset(pub.mainAddress & BLOCK_OFFSET_MASK),
										 secondaryOffset(pub.secondaryAddress & BLOCK_OFFSET_MASK), transactionID(0) {}

	void print(FILE * file = stdout) const
	{
		switch(command)
		{
			case LOAD_AND_FLUSH:
			{
#ifdef PRINT_REAL_INSTRUCTIONS
				fprintf(file, "{LOAD_AND_FLUSH, 0x%x},\n", static_cast<unsigned int>(mainBlock.value | mainBlockOffset));
#else
				fprintf(file, "Loading 0x%x to TMP (0x%x) then erasing\n", static_cast<unsigned int>(mainBlock.value | mainBlockOffset), static_cast<unsigned int>(TMP_BUF.getAddress()));
#endif
				break;
			}
			case COMMIT:
			{
#ifdef PRINT_REAL_INSTRUCTIONS
				fprintf(file, "{COMMIT, 0x%x},\n", static_cast<unsigned int>(mainBlock.value | mainBlockOffset));
#else
				fprintf(file, "Writing TMP to 0x%x (0x%x)\n", static_cast<unsigned int>(TMP_BUF.getAddress()), static_cast<unsigned int>(mainBlock.value | mainBlockOffset));
#endif
				break;
			}
			case FLUSH_AND_PARTIAL_COMMIT:
			{
#ifdef PRINT_REAL_INSTRUCTIONS
				fprintf(file, "{FLUSH_AND_PARTIAL_COMMIT, 0x%x, 0x%x},\n", static_cast<unsigned int>(mainBlock.value), static_cast<unsigned  int>(length));
#else
				fprintf(file, "Wipping 0x%x then writting TMP (0x%x)\n", static_cast<unsigned int>(mainBlock.value | mainBlockOffset), static_cast<unsigned int>(TMP_BUF.getAddress()));
#endif
				break;
			}
			case ERASE:
			{
#ifdef PRINT_REAL_INSTRUCTIONS
				fprintf(file, "{ERASE, 0x%x},\n", static_cast<unsigned int>(mainBlock.value | mainBlockOffset));
#else
				fprintf(file, "Erasing block 0x%x\n", static_cast<unsigned int>(mainBlock.value));
#endif
				break;
			}
			case COPY:
			{
#ifdef PRINT_REAL_INSTRUCTIONS
				if(secondaryOffset != 0)
					fprintf(file, "{COPY, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x},\n", static_cast<unsigned int>(mainBlock.value), static_cast<unsigned int>(mainBlockOffset), static_cast<unsigned int>(length), static_cast<unsigned int>(secondaryBlock.value),
					   static_cast<unsigned int>(secondaryOffset));
				else
					fprintf(file, "{COPY, 0x%x, 0x%x, 0x%x, 0x%x},\n", static_cast<unsigned int>(mainBlock.value), static_cast<unsigned int>(mainBlockOffset), static_cast<unsigned int>(length), static_cast<unsigned int>(secondaryBlock.value));

#else
				fprintf(file, "Copying %zu bytes from 0x%x to 0x%x\n", length, static_cast<unsigned int>(mainBlock.value | mainBlockOffset), static_cast<unsigned int>(secondaryBlock.value | secondaryOffset));
#endif
				break;
			}
			case CHAINED_COPY:
			{
#ifdef PRINT_REAL_INSTRUCTIONS
				fprintf(file, "{CHAINED_COPY, 0x%x, 0x%x, 0x%x},\n", static_cast<unsigned int>(mainBlock.value), static_cast<unsigned int>(mainBlockOffset), static_cast<unsigned int>(length));
#else
				fprintf(file, "Chained copy from 0x%x for %zu bytes\n", static_cast<unsigned int>(mainBlock.value | mainBlockOffset), length);
#endif
				break;
			}

			case CHAINED_COPY_SKIP:
			{
#ifdef PRINT_REAL_INSTRUCTIONS
				fprintf(file, "{CHAINED_COPY_SKIP, 0x%x},\n", static_cast<unsigned int>(length));
#else
				fprintf(file, "Skipping the next %zu bytes from chained copy\n", length);
#endif
				break;
			}
			case USE_BLOCK:
			{
#ifdef PRINT_REAL_INSTRUCTIONS
				fprintf(file, "{USE_BLOCK, 0x%x},\n", static_cast<unsigned int>(mainBlock.value));
#else
				fprintf(file, "Interpret implicit main block as 0x%x from now on\n", static_cast<unsigned int>(mainBlock.value));
#endif
				break;
			}

			case RELEASE_BLOCK:
			{
#ifdef PRINT_REAL_INSTRUCTIONS
				fprintf(file, "{RELEASE_BLOCK},\n");
#else
				fprintf(file, "Stop interpreting implicit main block\n");
#endif
				break;
			}

			case REBASE:
			{
#ifdef PRINT_REAL_INSTRUCTIONS
				fprintf(file, "{REBASE, 0x%x, 0x%x},\n", static_cast<unsigned int>(mainBlock.value), static_cast<unsigned int>(length));
#else
				fprintf(file, "Rebasing from block 0x%x for %zu blocks\n", static_cast<unsigned int>(mainBlock.value), length);
#endif
				break;
			}

			case END_OF_STREAM:
				break;
		}
	}

	bool operator==(const Command & b) const
	{
		return command == b.command &&
				mainBlock == b.mainBlock &&
				mainBlockOffset == b.mainBlockOffset &&
				length == b.length &&
				secondaryBlock == b.secondaryBlock &&
				secondaryOffset == b.mainBlockOffset;
	}

	bool operator!=(const Command & b) const
	{
		return command != b.command ||
			   mainBlock != b.mainBlock ||
			   mainBlockOffset != b.mainBlockOffset ||
			   length != b.length ||
			   secondaryBlock != b.secondaryBlock ||
			   secondaryOffset != b.secondaryOffset;
	}

	bool isLoad() const
	{
		return command == COPY && length == BLOCK_SIZE
			   && mainBlockOffset == 0 && secondaryOffset == 0
			   && mainBlock != TMP_BUF && secondaryBlock == TMP_BUF;
	}

	bool isCommitLike() const
	{
		return command == COMMIT || command == FLUSH_AND_PARTIAL_COMMIT;
	}

	bool isEraseLike(bool noWriteBack = false) const
	{
		return command == ERASE || command == LOAD_AND_FLUSH || (!noWriteBack && command == FLUSH_AND_PARTIAL_COMMIT);
	}

	void performTrivialOptimization();
	bool couldConvertToChainCopyWithPrevious(const Command & prev, const Address & endPreviousCopy) const;

	PublicCommand getPublicCommand() const
	{
		PublicCommand output;

		output.command = command;
		output.mainAddress = Address(mainBlock, mainBlockOffset).getAddress();
		output.length = length;
		output.secondaryAddress = Address(secondaryBlock, secondaryOffset).getAddress();

		return output;
	}
};

class SchedulerData
{
	size_t currentTransaction;
	bool transactionInProgress;
	vector<Command> commands;

public:

	bool wantLog;

	void insertCommand(Command command);

	void newTransaction()
	{
		currentTransaction += 1;
		transactionInProgress = true;
	}

	void finishTransaction()
	{
		transactionInProgress = false;
	}

	void createChains();
	void commitUseBlockSection(vector<Command> &newCommands, size_t instructionIgnore, bool &hadBlock, const BlockID &blockChain, vector<Command>::const_iterator &startChain, const vector<Command>::const_iterator &iter) const;
	void addUseBlocks();

	void generateInstructions(vector<PublicCommand> & output)
	{
#ifndef DISABLE_CHAINED_COPY
		createChains();
#endif
		addUseBlocks();

		//Once we're done, we normalize REBASEs
		normalizeRebase();

		output.reserve(commands.size());
		for(const auto & command : commands)
			output.emplace_back(command.getPublicCommand());
	}

	void printStats(const vector<PublicCommand> & publicCommands) const
	{
		//Compute the length of the encoded instructions
		size_t length;
		{
			uint8_t * bytes = nullptr;
			Encoder().encode(publicCommands, bytes, length);
			free(bytes);
		}

		cout << "Use of " << commands.size() << " commands, using a total of " << length << " bytes." << endl;

		map<BlockID, size_t> blocks;

		for(const auto & command : commands)
		{
			if(command.isEraseLike())
				blocks[command.mainBlock] += 1;
		}

		bool isFirst = true;
		size_t singleErase = 0;
		for(const auto & block : blocks)
		{
			if(block.second > 1)
			{
				if(isFirst)
				{
					cout << "Erasing ";
					isFirst = false;
				}
				else
					cout << ", ";

				cout << "block #" << (block.first.value >> 12u)  << " " << block.second << " times";
			}
			else
				singleErase += 1;
		}

		if(!isFirst)
			cout << endl;

		if(singleErase > 0)
			cout << "A total of " << singleErase << " blocks went through a single erase!" << endl;
	}

	void updateLastRebase();

	void normalizeRebase()
	{
		for(auto & command : commands)
		{
			if(command.command == REBASE)
				command.length = largestPossibleValue(numberOfBitsNecessary(command.length));
		}
	}

	SchedulerData() : currentTransaction(0), transactionInProgress(false), commands(), wantLog(false) {}

};

#include "Token.h"
#include "Block.h"
#include "DetailedBlock.h"
#include "network.h"

bool buildBlockVector(const vector<BSDiffMoves> & input, vector<Block> & output);

namespace Scheduler
{
	//Passes
	void removeSelfReferencesOnly(vector <Block> & blocks, SchedulerData & commands);
	void removeUnidirectionnalReferences(vector<Block> & blocks, SchedulerData & commands);
	void removeNetworks(vector<Block> & blocks, SchedulerData & commands);

	typedef function<void(const BlockID&, bool, VirtualMemory&, SchedulerData&)> SwapOperator;
	typedef function<DetailedBlock(bool)> FirstNodeLayoutGenerator;

	//Codegen utils
	void halfSwapCodeGeneration(NetworkNode & firstNode, NetworkNode & secNode, VirtualMemory & memoryLayout, SchedulerData & commands);
	void partialSwapCodeGeneration(const NetworkNode & firstNode, const NetworkNode & secNode, SwapOperator swapOperator, FirstNodeLayoutGenerator firstNodeLayout, VirtualMemory & memoryLayout, SchedulerData & commands, bool canReorder);
	void reorderingSwapCodeGeneration(NetworkNode & firstNode, NetworkNode & secondaryNode, VirtualMemory & memoryLayout, SchedulerData & commands);
	void networkSwapCodeGeneration(const NetworkNode & firstNode, const NetworkNode & secNode, VirtualMemory & memoryLayout, SchedulerData & commands);
	void pullDataToNode(const NetworkNode & node, VirtualMemory & memoryLayout, SchedulerData & commands);

	//Utils
	void interpretBlockSort(const Block & block, SchedulerData & commands, bool transactionBundling = true);
	size_t indexOfBlockID(const vector<Block> & block, const BlockID & blockID);

	void extractNetwork(const vector<Block> & blocks, const size_t & baseBlockIndex, vector<size_t> & output);
}

#endif //HERMES_SCHEDULER_H
