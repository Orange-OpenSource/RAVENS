//
// Created by Emile-Hugo Spir on 3/19/18.
//

#include "scheduler.h"

bool validateStaticResults(const vector<PublicCommand> &real, const vector<Command> &expectation)
{
	const size_t lengthExpected = expectation.size();
	const size_t lengthReal = real.size();
	bool testFail = lengthReal != lengthExpected;

	if(!testFail)
	{

		for(size_t i = 0; i < lengthExpected; ++i)
		{
			if(expectation[i] != real[i])
			{
				cout << "Test failure: instruction #" << i << " doesn't match expectation!" << endl;
				cout << "Expected : ";
				expectation[i].print();
				cout << "Generated : ";
				Command(real[i]).print();
				testFail = true;
			}
		}

		if(testFail)
		{
			cout << endl;
		}
#ifdef VERBOSE_STATIC_TESTS
		else
		{
			cout << "Test successful!" << endl << endl;
		}
#endif
	}
	else
	{
		if(lengthReal > lengthExpected)
			cout << "Test failure : too many instructions (" << lengthReal << " > " << lengthExpected << ")" << endl;
		else
			cout << "Test failure : not enough instructions (" << lengthReal << " < " << lengthExpected << ")" << endl;

		for(const auto & instruction : real)
			Command(instruction).print();

		cout << endl;
	}

	return !testFail;
}

bool firstPassTest()
{
#ifdef VERBOSE_STATIC_TESTS
	cout << "Testing the simple reorder pass" << endl;
#endif

	const vector<BSDiffMoves> input = {{100, 100, 400},
								  {100, 100, 100},
								  {400, 200, 800}};

	const vector<Command> expected = {{USE_BLOCK, 0x0},
								{LOAD_AND_FLUSH, 0x0},
								{COPY, TMP_BUF, 0x64, 0x64, 0x0, 0x64},
								{COPY, TMP_BUF, 0x64, 0x64, 0x0, 0x190},
								{COPY, TMP_BUF, 0x190, 0xc8, 0x0, 0x320}};

	vector<PublicCommand> output;

	schedule(input, output);
	return validateStaticResults(output, expected);
}

bool secondPassTest()
{
#ifdef VERBOSE_STATIC_TESTS
	cout << "Testing the simple dependency pass" << endl;
#endif


	const vector<BSDiffMoves> input = {{100, 100, 400},
										{100, 100, BLOCK_SIZE + 100}};

	const vector<Command> expected = {{REBASE, 0x0, 0x1},
									  {ERASE, 0x1000},
									  {USE_BLOCK, 0x0},
									  {COPY, 0x0, 0x64, 0x64, 0x1000, 0x64},
									  {LOAD_AND_FLUSH, 0x0},
									  {COPY, TMP_BUF, 0x64, 0x64, 0x0, 0x190}};
	vector<PublicCommand> output;
	schedule(input, output);
	return validateStaticResults(output, expected);
}

bool thirdPassSimpleChain()
{

	/*
	 * 			.---------------------------.
	 * 			|							|
	 * 			v							|
	 * .-------------- A -------------.		|
	 * |							  |		|
	 * | 100 | 100 | BLOCK_SIZE - 200 |		|
	 * |							  |		|
	 * '-------------- A -------------'		|
	 * 			|							|
	 * 			v							|
	 * .-------------- B -------------.		|
	 * |							  |		|
	 * | 200 | 100 | 100 |  BS - 300  |		|
	 * |							  |		|
	 * '-------------- B -------------'		|
	 * 			|							|
	 * 			v							|
	 * .-------------- C -------------.		|
	 * |							  |		|
	 * | 300 | 100 | BLOCK_SIZE - 400 |		|
	 * |							  |		|
	 * '-------------- C -------------'		|
	 * 			|							|
	 * 			v							|
	 * .-------------- D -------------.		|
	 * |							  |		|
	 * | 400 | 100 | BLOCK_SIZE - 500 |		|
	 * |							  |		|
	 * '-------------- D -------------'		|
	 * 			`---------------------------'
	 *
	 */

#ifdef VERBOSE_STATIC_TESTS
	cout << "Testing the cycle pass" << endl;
#endif

	const vector<BSDiffMoves> input = {{100, 100, BLOCK_SIZE + 200},
										{BLOCK_SIZE + 200, 100, 2 * BLOCK_SIZE + 300},
										{2 * BLOCK_SIZE + 300, 100, 3 * BLOCK_SIZE + 400},
										{3 * BLOCK_SIZE + 400, 100, 100}};

	const vector<Command> expected = {{REBASE, 0x0, 0x3},
									  {COPY, 0x1000, 0xc8, 0x64, TMP_BUF},
									  {ERASE, 0x1000},
									  {COPY, 0x0, 0x64, 0x64, 0x1000, 0xc8},
									  {ERASE, 0x0},
									  {COPY, 0x3000, 0x190, 0x64, 0x0, 0x64},
									  {ERASE, 0x3000},
									  {USE_BLOCK, 0x2000},
									  {COPY, 0x2000, 0x12c, 0x64, 0x3000, 0x190},
									  {ERASE, 0x2000},
									  {COPY, TMP_BUF, 0x0, 0x64, 0x2000, 0x12c}};

	vector<PublicCommand> output;
	schedule(input, output);
	return validateStaticResults(output, expected);
}

bool thirdPassTestWithFullRecovery()
{

	/*
	 * 			.---------------------------.
	 * 			|							|
	 * 			v							|
	 * .-------------- A -------------.		|
	 * |							  |		|
	 * | 100 | 100 | BLOCK_SIZE - 200 |		|
	 * |							  |		|
	 * '-------------- A -------------'		|
	 * 			|							|
	 * 			v							|
	 * .-------------- B -------------.		|
	 * |							  |		|
	 * | 100 | 100 | 100 |  BS - 200  |		|
	 * |							  |		|
	 * '-------------- B -------------'		|
	 * 			|							|
	 * 			v							|
	 * .-------------- C -------------.		|
	 * |							  |		|
	 * | 100 | 100 | BLOCK_SIZE - 200 |		|
	 * |							  |		|
	 * '-------------- C -------------'		|
	 * 			|							|
	 * 			v							|
	 * .-------------- D -------------.		|
	 * |							  |		|
	 * | 100 | 100 | BLOCK_SIZE - 200 |		|
	 * |							  |		|
	 * '-------------- D -------------'		|
	 * 			`---------------------------'
	 *
	 */

#ifdef VERBOSE_STATIC_TESTS
	cout << "Testing the cycle with full reuse pass" << endl;
#endif

	const vector<BSDiffMoves> input = {{BLOCK_SIZE, 100, BLOCK_SIZE},
										{100, 100, BLOCK_SIZE + 100},
										{BLOCK_SIZE + 200, BLOCK_SIZE - 200, BLOCK_SIZE + 200},

										{2 * BLOCK_SIZE, 100, 2 * BLOCK_SIZE},
										{BLOCK_SIZE + 100, 100, 2 * BLOCK_SIZE + 100},
										{2 * BLOCK_SIZE + 200, BLOCK_SIZE - 200, 2 * BLOCK_SIZE + 200},

										{3 * BLOCK_SIZE, 100, 3 * BLOCK_SIZE},
										{2 * BLOCK_SIZE + 100, 100, 3 * BLOCK_SIZE + 100},
										{3 * BLOCK_SIZE + 200, BLOCK_SIZE - 200, 3 * BLOCK_SIZE + 200},

										{0, 100, 0},
										{3 * BLOCK_SIZE + 100, 100, 100},
										{200, BLOCK_SIZE - 200, 200}};

	const vector<Command> expected = {{REBASE, 0x0, 0x3},
									  {LOAD_AND_FLUSH, 0x1000},
									  {COPY, TMP_BUF, 0x0, 0x64, 0x1000},
									  {USE_BLOCK, 0x0},
									  {CHAINED_COPY, 0x0, 0x64, 0x64},
									  {CHAINED_COPY, TMP_BUF, 0xc8, 0xf38},
									  {COPY, 0x0, 0x0, 0x64, TMP_BUF},
									  {COPY, 0x0, 0xc8, 0xf38, TMP_BUF, 0xc8},
									  {FLUSH_AND_PARTIAL_COMMIT, 0x0, 0x64},
									  {USE_BLOCK, 0x3000},
									  {CHAINED_COPY, 0x3000, 0x64, 0x64},
									  {CHAINED_COPY, TMP_BUF, 0xc8, 0xf38},
									  {COPY, 0x3000, 0x0, 0x64, TMP_BUF},
									  {COPY, 0x3000, 0xc8, 0xf38, TMP_BUF, 0xc8},
									  {FLUSH_AND_PARTIAL_COMMIT, 0x3000, 0x64},
									  {USE_BLOCK, 0x2000},
									  {CHAINED_COPY, 0x2000, 0x64, 0x64},
									  {CHAINED_COPY, TMP_BUF, 0xc8, 0xf38},
									  {COPY, 0x2000, 0x0, 0x64, TMP_BUF},
									  {COPY, 0x2000, 0xc8, 0xf38, TMP_BUF, 0xc8},
									  {FLUSH_AND_PARTIAL_COMMIT, 0x2000, BLOCK_SIZE}};

	vector<PublicCommand> output;
	schedule(input, output);
	return validateStaticResults(output, expected);
}

bool thirdPassTestWithExternalReference()
{

	/*
	 *
	 * 			.---------------------------.
	 * 			|							|
	 * 			v							|
	 * .-------------- A -------------.		|
	 * |							  |		|
	 * | 100 | 100 | BLOCK_SIZE - 200 |		|
	 * |							  |		|
	 * '-------------- A -------------'		|
	 * 			|							|
	 * 			|	  ,---------------------+---.
	 * 			v	  v						|	|
	 * .-------------- B -------------.		|	|
	 * |							  |		|	|
	 * | 100 | 100 | 100 |  BS - 200  |		|	|
	 * |							  |		|	|
	 * '-------------- B -------------'		|	|
	 * 			|							|	|
	 * 			v							|	|
	 * .-------------- C -------------.		|	|
	 * |							  |		|	|
	 * | 100 | 100 | BLOCK_SIZE - 200 |		|	|
	 * |							  |		|	|
	 * '-------------- C -------------'		|	|
	 * 			|							|	|
	 * 			v							|	|
	 * .-------------- D -------------.		|	|
	 * |							  |		|	|
	 * | 100 | 100 | BLOCK_SIZE - 200 |		|	|
	 * |							  |		|	|
	 * '-------------- D -------------'		|	|
	 * 			`---------------------------'	|
	 *											|
	 * .-------------- E -------------.			|
	 * |							  |			|
	 * | 100 | 100 | BLOCK_SIZE - 200 |			|
	 * |							  |			|
	 * '-------------- E -------------'			|
	 * 			`-------------------------------'
	 *
	 */

#ifdef VERBOSE_STATIC_TESTS
	cout << "Testing the cycle pass with external references" << endl;
#endif

	const vector<BSDiffMoves> input = {{100, 100, BLOCK_SIZE + 100},
										{4 * BLOCK_SIZE + 100, 100, BLOCK_SIZE + 200},
										{BLOCK_SIZE + 100, 100, 2 * BLOCK_SIZE + 100},
										{2 * BLOCK_SIZE + 100, 100, 3 * BLOCK_SIZE + 100},
										{3 * BLOCK_SIZE + 100, 100, 100}};

	const vector<Command> expected = {{REBASE, 0x0, 0x7},
									  {COPY, 0x1000, 0x64, 0x64, TMP_BUF},
									  {ERASE, 0x1000},
									  {COPY, 0x0, 0x64, 0x64, 0x1000, 0x64},
									  {CHAINED_COPY, 0x4000, 0x64, 0x64},
									  {ERASE, 0x0},
									  {COPY, 0x3000, 0x64, 0x64, 0x0, 0x64},
									  {ERASE, 0x3000},
									  {USE_BLOCK, 0x2000},
									  {COPY, 0x2000, 0x64, 0x64, 0x3000, 0x64},
									  {ERASE, 0x2000},
									  {COPY, TMP_BUF, 0x0, 0x64, 0x2000, 0x64}};

	vector<PublicCommand> output;
	schedule(input, output);
	return validateStaticResults(output, expected);
}

#define FRAGMENT (BLOCK_SIZE >> 2u)

bool forthPassTest()
{
#ifdef VERBOSE_STATIC_TESTS
	cout << "Testing the complex network pass" << endl;
#endif

	/*
	 * Utter mess of a test scenario
	 *
	 * Each block are cut in for equal parts, each chunk destination is coded (finalBlock / blockRank)
	 *
	 * .-------------- A --------------+-------------- B --------------+-------------- C --------------+-------------- D ------------------------.
	 * |							   |							   |							   |							   			 |
	 * | B / 4 | C / 4 | D / 2 | A / 1 | A / 3 | B / 2 | C / 1 | D / 1 | D / 3 | A / 4 | B / 1 | C / 2 | C / 3 | D / 4 | A / 2 | B / 3 | A / 2.5 |
	 * |							   |							   |							   |							   			 |
	 * '-------------- A --------------+-------------- B --------------+-------------- C --------------+-------------- D ------------------------'
	 */

	const vector<BSDiffMoves> input = {{0 * BLOCK_SIZE + 0 * FRAGMENT, FRAGMENT, 1 * BLOCK_SIZE + 3 * FRAGMENT},
										{0 * BLOCK_SIZE + 1 * FRAGMENT, FRAGMENT, 2 * BLOCK_SIZE + 3 * FRAGMENT},
										{0 * BLOCK_SIZE + 2 * FRAGMENT, FRAGMENT, 3 * BLOCK_SIZE + 1 * FRAGMENT},
										{0 * BLOCK_SIZE + 3 * FRAGMENT, FRAGMENT, 0 * BLOCK_SIZE + 0 * FRAGMENT},
										{1 * BLOCK_SIZE + 0 * FRAGMENT, FRAGMENT, 0 * BLOCK_SIZE + 2 * FRAGMENT},
										{1 * BLOCK_SIZE + 1 * FRAGMENT, FRAGMENT, 1 * BLOCK_SIZE + 1 * FRAGMENT},
										{1 * BLOCK_SIZE + 2 * FRAGMENT, FRAGMENT, 2 * BLOCK_SIZE + 0 * FRAGMENT},
										{1 * BLOCK_SIZE + 3 * FRAGMENT, FRAGMENT, 3 * BLOCK_SIZE + 0 * FRAGMENT},
										{2 * BLOCK_SIZE + 0 * FRAGMENT, FRAGMENT, 3 * BLOCK_SIZE + 2 * FRAGMENT},
										{2 * BLOCK_SIZE + 1 * FRAGMENT, FRAGMENT, 0 * BLOCK_SIZE + 3 * FRAGMENT},
										{2 * BLOCK_SIZE + 2 * FRAGMENT, FRAGMENT, 1 * BLOCK_SIZE + 0 * FRAGMENT},
										{2 * BLOCK_SIZE + 3 * FRAGMENT, FRAGMENT, 2 * BLOCK_SIZE + 1 * FRAGMENT},
										{3 * BLOCK_SIZE + 0 * FRAGMENT, FRAGMENT, 2 * BLOCK_SIZE + 2 * FRAGMENT},
										{3 * BLOCK_SIZE + 1 * FRAGMENT, FRAGMENT, 3 * BLOCK_SIZE + 3 * FRAGMENT},
										{3 * BLOCK_SIZE + 2 * FRAGMENT, FRAGMENT / 2, 0 * BLOCK_SIZE + 2 * (FRAGMENT / 2)},
										{3 * BLOCK_SIZE + 5 * (FRAGMENT / 2), FRAGMENT, 1 * BLOCK_SIZE + 2 * FRAGMENT},
										{3 * BLOCK_SIZE + 7 * (FRAGMENT / 2), FRAGMENT / 2, 0 * BLOCK_SIZE + 3 * (FRAGMENT / 2)}};

	const vector<Command> expected = {{REBASE, 0x0, 0x3},
			//Exchange [0] and [1]
			{LOAD_AND_FLUSH, 0x1000},
			{COPY, TMP_BUF, 0x400, 0x400, 0x1000},
			{CHAINED_COPY, TMP_BUF, 0xc00, 0x400},
			{USE_BLOCK, 0x0},
			{CHAINED_COPY, 0x0, 0x0, 0x400},
			{CHAINED_COPY, 0x0, 0x800, 0x400},
			{COPY, 0x0, 0xc00, 0x400, TMP_BUF, 0x400},
			{COPY, 0x0, 0x400, 0x400, TMP_BUF, 0xc00},
			{FLUSH_AND_PARTIAL_COMMIT, 0x0, BLOCK_SIZE},
			{RELEASE_BLOCK},

			//Exchange [2] and [3]
			{LOAD_AND_FLUSH, 0x3000},
			{COPY, TMP_BUF, 0x400, 0x400, 0x3000},
			{CHAINED_COPY, TMP_BUF, 0xa00, 0x400},
			{USE_BLOCK, 0x2000},
			{CHAINED_COPY, 0x2000, 0x0, 0x400},
			{CHAINED_COPY, 0x2000, 0x800, 0x400},

			//Merger with [0] and [2] exchange
			{COPY, 0x2000, 0x400, 0x400, TMP_BUF, 0x400},
			{COPY, 0x2000, 0xc00, 0x400, TMP_BUF, 0xa00},
			{ERASE, 0x2000},
			{USE_BLOCK, 0x0},
			{COPY, 0x0, 0x800, 0x400, 0x2000},
			{CHAINED_COPY, TMP_BUF, 0xa00, 0x400},
			{CHAINED_COPY, TMP_BUF, 0x0, 0x400},
			{CHAINED_COPY, 0x0, 0xc00, 0x400},

			//Write [0]
			{COPY, 0x0, 0x0, 0x400, TMP_BUF},
			{COPY, 0x0, 0x400, 0x400, TMP_BUF, 0xa00},
			{ERASE, 0x0},
			{COPY, TMP_BUF, 0xa00, 0x400, 0x0, 0x0},
			{CHAINED_COPY, TMP_BUF, 0x800, 0x200},
			{CHAINED_COPY, TMP_BUF, 0xe00, 0x200},
			{CHAINED_COPY, TMP_BUF, 0x0, 0x800},

			//Exchange [1] and [3]
			{RELEASE_BLOCK},
			{LOAD_AND_FLUSH, 0x1000},
			{USE_BLOCK, 0x3000},
			{COPY, 0x3000, 0xc00, 0x400, 0x1000},
			{CHAINED_COPY, TMP_BUF, 0x0, 0x400},
			{CHAINED_COPY, 0x3000, 0x400, 0x400},
			{CHAINED_COPY, TMP_BUF, 0x800, 0x400},
			{COPY, 0x3000, 0x0, 0x400, TMP_BUF},
			{COPY, 0x3000, 0x800, 0x400, TMP_BUF, 0x800},
			{ERASE, 0x3000},
			{COPY, TMP_BUF, 0x400, 0x400, 0x3000},
			{CHAINED_COPY, TMP_BUF, 0xc00, 0x400},
			{CHAINED_COPY, TMP_BUF, 0x800, 0x400},
			{CHAINED_COPY, TMP_BUF, 0x0, 0x400}};

	vector<PublicCommand> output;
	schedule(input, output);
	return validateStaticResults(output, expected);
}

bool forthPassTestWithCompetitiveRead()
{
#ifdef VERBOSE_STATIC_TESTS
	cout << "Testing the complex network pass with competitive read" << endl;
#endif

	/*
	 * Utter mess of a test scenario
	 *
	 * Each block are cut in equal parts, each chunk destination is coded (finalBlock / blockRank), the third block originated from D is used by two different blocks
	 *
	 * .-------------- A --------------+-------------- B --------------+-------------- C --------------+-------------- D ------------------.
	 * |							   |							   |							   |								   |
	 * | B / 4 | C / 4 | D / 2 | A / 1 | A / 3 | B / 2 | C / 1 | D / 2 | D / 3 | A / 4 | B / 1 | C / 2 |  OSEF | D / 4 | A/2 & C/3 | B / 3 |
	 * |							   |							   |							   |							   	   |
	 * '-------------- A --------------+-------------- B --------------+-------------- C --------------+-------------- D ------------------'
	 */

	const vector<BSDiffMoves> input = {{0 * BLOCK_SIZE + 0 * FRAGMENT, FRAGMENT, 1 * BLOCK_SIZE + 3 * FRAGMENT},
										{0 * BLOCK_SIZE + 1 * FRAGMENT, FRAGMENT, 2 * BLOCK_SIZE + 3 * FRAGMENT},
										{0 * BLOCK_SIZE + 2 * FRAGMENT, FRAGMENT, 3 * BLOCK_SIZE + 1 * FRAGMENT},
										{0 * BLOCK_SIZE + 3 * FRAGMENT, FRAGMENT, 0 * BLOCK_SIZE + 0 * FRAGMENT},
										{1 * BLOCK_SIZE + 0 * FRAGMENT, FRAGMENT, 0 * BLOCK_SIZE + 2 * FRAGMENT},
										{1 * BLOCK_SIZE + 1 * FRAGMENT, FRAGMENT, 1 * BLOCK_SIZE + 1 * FRAGMENT},
										{1 * BLOCK_SIZE + 2 * FRAGMENT, FRAGMENT, 2 * BLOCK_SIZE + 0 * FRAGMENT},
										{1 * BLOCK_SIZE + 3 * FRAGMENT, FRAGMENT, 3 * BLOCK_SIZE + 0 * FRAGMENT},
										{2 * BLOCK_SIZE + 0 * FRAGMENT, FRAGMENT, 3 * BLOCK_SIZE + 2 * FRAGMENT},
										{2 * BLOCK_SIZE + 1 * FRAGMENT, FRAGMENT, 0 * BLOCK_SIZE + 3 * FRAGMENT},
										{2 * BLOCK_SIZE + 2 * FRAGMENT, FRAGMENT, 1 * BLOCK_SIZE + 0 * FRAGMENT},
										{2 * BLOCK_SIZE + 3 * FRAGMENT, FRAGMENT, 2 * BLOCK_SIZE + 1 * FRAGMENT},
										{3 * BLOCK_SIZE + 1 * FRAGMENT, FRAGMENT, 3 * BLOCK_SIZE + 3 * FRAGMENT},
										{3 * BLOCK_SIZE + 2 * FRAGMENT, FRAGMENT, 0 * BLOCK_SIZE + 1 * FRAGMENT},
										{3 * BLOCK_SIZE + 2 * FRAGMENT, FRAGMENT, 2 * BLOCK_SIZE + 2 * FRAGMENT},
										{3 * BLOCK_SIZE + 3 * FRAGMENT, FRAGMENT, 1 * BLOCK_SIZE + 2 * FRAGMENT}};

	const vector<Command> expected = {{REBASE, 0x0, 0x3},
			//Exchange [0] and [1]
			{LOAD_AND_FLUSH, 0x1000},
			{COPY, TMP_BUF, 0x400, 0x400, 0x1000},
			{CHAINED_COPY, TMP_BUF, 0xc00, 0x400},
			{USE_BLOCK, 0x0},
			{CHAINED_COPY, 0x0, 0x0, 0x400},
			{CHAINED_COPY, 0x0, 0x800, 0x400},
			{COPY, 0x0, 0xc00, 0x400, TMP_BUF, 0x400},
			{COPY, 0x0, 0x400, 0x400, TMP_BUF, 0xc00},
			{FLUSH_AND_PARTIAL_COMMIT, 0x0, BLOCK_SIZE},
			{RELEASE_BLOCK},

			//Exchange [2] and [3]
			{COPY, 0x3000, 0x400, 0xc00, TMP_BUF},
			{FLUSH_AND_PARTIAL_COMMIT, 0x3000, 0x400},
			{USE_BLOCK, 0x2000},
			{CHAINED_COPY, 0x2000, 0x0, 0x800},
			{COPY, 0x2000, 0x800, 0x400, TMP_BUF},
			{COPY, 0x2000, 0xc00, 0x400, TMP_BUF, 0xc00},
			{FLUSH_AND_PARTIAL_COMMIT, 0x2000, BLOCK_SIZE},
			{RELEASE_BLOCK},

			//Exchange [0] and [3]
			{LOAD_AND_FLUSH, 0x0},
			{COPY, TMP_BUF, 0x400, 0x400, 0x0},
			{CHAINED_COPY, 0x2000, 0x400, 0x400},
			{CHAINED_COPY, TMP_BUF, 0x0, 0x400},
			{USE_BLOCK, 0x3000},
			{CHAINED_COPY, 0x3000, 0x800, 0x400},
			{COPY, 0x3000, 0x400, 0x400, TMP_BUF},
			{CHAINED_COPY, 0x3000, 0x0, 0x400},

			//Merged write of [3] with [1] and [3] exchange
			{ERASE, 0x3000},
			{USE_BLOCK, 0x1000},
			{COPY, 0x1000, 0x400, 0x400, 0x3000},
			{CHAINED_COPY, 0x1000, 0xc00, 0x400},
			{CHAINED_COPY, TMP_BUF, 0x0, 0x800},

			//Merged write of [1] with [1] and [2] exchange
			{COPY, 0x1000, 0x800, 0x400, TMP_BUF},
			{CHAINED_COPY, 0x1000, 0x0, 0x400},
			{ERASE, 0x1000},
			{USE_BLOCK, 0x2000},
			{COPY, 0x2000, 0x0, 0x400, 0x1000},
			{CHAINED_COPY, TMP_BUF, 0x400, 0x400},
			{CHAINED_COPY, 0x2000, 0x800, 0x400},
			{CHAINED_COPY, TMP_BUF, 0x0, 0x400},

			//End of [1] and [2] exchange
			{COPY, 0x2000, 0xc00, 0x400, TMP_BUF},
			{ERASE, 0x2000},
			{COPY, TMP_BUF, 0x800, 0x400, 0x2000},
			{CHAINED_COPY, TMP_BUF, 0x0, 0x400},
			{RELEASE_BLOCK},
			{CHAINED_COPY, 0x0, 0x400, 0x400},
			{CHAINED_COPY, TMP_BUF, 0xc00, 0x400}};

	vector<PublicCommand> output;
	schedule(input, output);
	return validateStaticResults(output, expected);
}

bool forthPassTestWithHarderCompetitiveRead()
{
#ifdef VERBOSE_STATIC_TESTS
	cout << "Testing the complex network pass with a harder competitive read" << endl;
#endif

	/*
	 * Utter mess of a test scenario
	 *
	 * Each block are cut in equal parts, each chunk destination is coded (finalBlock / blockRank), the third block originated from D is used by two different blocks
	 *
	 * .-------------- A --------------+-------------- B --------------+-------------- C --------------+-------------- D ------------------.
	 * |							   |							   |							   |								   |
	 * | B / 4 | C / 4 | D / 2 | A / 1 | A / 3 | B / 2 | C / 1 | D / 2 | D / 3 | A / 4 | B / 1 | C / 2 |  OSEF | D / 4 | A/2 & B/3 | C / 3 |
	 * |							   |							   |							   |							   	   |
	 * '-------------- A --------------+-------------- B --------------+-------------- C --------------+-------------- D ------------------'
	 */

	const vector<BSDiffMoves> input = {{0 * BLOCK_SIZE + 0 * FRAGMENT, FRAGMENT, 1 * BLOCK_SIZE + 3 * FRAGMENT},
										{0 * BLOCK_SIZE + 1 * FRAGMENT, FRAGMENT, 2 * BLOCK_SIZE + 3 * FRAGMENT},
										{0 * BLOCK_SIZE + 2 * FRAGMENT, FRAGMENT, 3 * BLOCK_SIZE + 1 * FRAGMENT},
										{0 * BLOCK_SIZE + 3 * FRAGMENT, FRAGMENT, 0 * BLOCK_SIZE + 0 * FRAGMENT},
										{1 * BLOCK_SIZE + 0 * FRAGMENT, FRAGMENT, 0 * BLOCK_SIZE + 2 * FRAGMENT},
										{1 * BLOCK_SIZE + 1 * FRAGMENT, FRAGMENT, 1 * BLOCK_SIZE + 1 * FRAGMENT},
										{1 * BLOCK_SIZE + 2 * FRAGMENT, FRAGMENT, 2 * BLOCK_SIZE + 0 * FRAGMENT},
										{1 * BLOCK_SIZE + 3 * FRAGMENT, FRAGMENT, 3 * BLOCK_SIZE + 0 * FRAGMENT},
										{2 * BLOCK_SIZE + 0 * FRAGMENT, FRAGMENT, 3 * BLOCK_SIZE + 2 * FRAGMENT},
										{2 * BLOCK_SIZE + 1 * FRAGMENT, FRAGMENT, 0 * BLOCK_SIZE + 3 * FRAGMENT},
										{2 * BLOCK_SIZE + 2 * FRAGMENT, FRAGMENT, 1 * BLOCK_SIZE + 0 * FRAGMENT},
										{2 * BLOCK_SIZE + 3 * FRAGMENT, FRAGMENT, 2 * BLOCK_SIZE + 1 * FRAGMENT},
										{3 * BLOCK_SIZE + 1 * FRAGMENT, FRAGMENT, 3 * BLOCK_SIZE + 3 * FRAGMENT},
										{3 * BLOCK_SIZE + 2 * FRAGMENT, FRAGMENT, 0 * BLOCK_SIZE + 1 * FRAGMENT},
										{3 * BLOCK_SIZE + 2 * FRAGMENT, FRAGMENT, 1 * BLOCK_SIZE + 2 * FRAGMENT},
										{3 * BLOCK_SIZE + 3 * FRAGMENT, FRAGMENT, 2 * BLOCK_SIZE + 2 * FRAGMENT}};

	const vector<Command> expected = {{REBASE, 0x0, 0x3},
			//Exchange [0] and [1]
			{LOAD_AND_FLUSH, 0x1000},
			{COPY, TMP_BUF, 0x400, 0x400, 0x1000},
			{CHAINED_COPY, TMP_BUF, 0xc00, 0x400},
			{USE_BLOCK, 0x0},
			{CHAINED_COPY, 0x0, 0x0, 0x400},
			{CHAINED_COPY, 0x0, 0x800, 0x400},
			{COPY, 0x0, 0xc00, 0x400, TMP_BUF, 0x400},
			{COPY, 0x0, 0x400, 0x400, TMP_BUF, 0xc00},
			{FLUSH_AND_PARTIAL_COMMIT, 0x0, BLOCK_SIZE},
			{RELEASE_BLOCK},

			//Exchange [2] and [3]
			{COPY, 0x3000, 0x400, 0xc00, TMP_BUF},
			{FLUSH_AND_PARTIAL_COMMIT, 0x3000, 0x400},
			{USE_BLOCK, 0x2000},
			{CHAINED_COPY, 0x2000, 0x0, 0x400},
			{CHAINED_COPY, 0x2000, 0x800, 0x400},

			//Merger with [0] and [2] exchange
			{COPY, 0x2000, 0x400, 0x400, TMP_BUF},
			{COPY, 0x2000, 0xc00, 0x400, TMP_BUF, 0xc00},
			{ERASE, 0x2000},
			{USE_BLOCK, 0x0},
			{COPY, 0x0, 0x800, 0x400, 0x2000},
			{CHAINED_COPY, TMP_BUF, 0xc00, 0x400},
			{CHAINED_COPY, TMP_BUF, 0x800, 0x400},
			{CHAINED_COPY, 0x0, 0xc00, 0x400},

			//Write [0]
			{COPY, 0x0, 0x0, 0x800, TMP_BUF, 0x800},
			{ERASE, 0x0},
			{COPY, TMP_BUF, 0xc00, 0x400, 0x0},
			{CHAINED_COPY, TMP_BUF, 0x400, 0x800},
			{CHAINED_COPY, TMP_BUF, 0x0, 0x400},
			{RELEASE_BLOCK},

			//Exchange [1] and [3]
			{LOAD_AND_FLUSH, 0x1000},
			{COPY, 0x3000, 0x800, 0x400, 0x1000},
			{CHAINED_COPY, TMP_BUF, 0x0, 0x400},
			{CHAINED_COPY, 0x0, 0x400, 0x400},
			{CHAINED_COPY, TMP_BUF, 0x800, 0x400},
			{USE_BLOCK, 0x3000},
			{COPY, 0x3000, 0x0, 0x400, TMP_BUF},
			{COPY, 0x3000, 0x400, 0x400, TMP_BUF, 0x800},
			{ERASE, 0x3000},
			{COPY, TMP_BUF, 0x400, 0x400, 0x3000},
			{CHAINED_COPY, TMP_BUF, 0xc00, 0x400},
			{CHAINED_COPY, TMP_BUF, 0x800, 0x400},
			{CHAINED_COPY, TMP_BUF, 0x0, 0x400}};

	vector<PublicCommand> output;
	schedule(input, output);
	return validateStaticResults(output, expected);
}

bool forthPassTestWithCompetitiveReadOnReusedSpace()
{
#ifdef VERBOSE_STATIC_TESTS
	cout << "Testing the complex network pass with a competitive read on space we're trying to reuse" << endl;
#endif

	/*
	 * Utter mess of a test scenario
	 *
	 * Each block are cut in equal parts, each chunk destination is coded (finalBlock / blockRank), the third block originated from D is used by two different blocks
	 *
	 * .-------------- A --------------+-------------- B --------------+-------------- C --------------+-------------- D ------------------.
	 * |							   |							   |							   |								   |
	 * | B / 4 | C / 4 | D / 2 | A / 1 | A / 3 | B / 2 | C / 1 | D / 2 | D / 3 | A / 4 | B / 1 | C / 2 |  OSEF | D / 4 | A/2 & B/3 | C / 3 |
	 * |							   |							   |							   |							   	   |
	 * '-------------- A --------------+-------------- B --------------+-------------- C --------------+-------------- D ------------------'
	 */

	const vector<BSDiffMoves> input = {{0 * BLOCK_SIZE + 0 * FRAGMENT, FRAGMENT, 1 * BLOCK_SIZE + 3 * FRAGMENT},
										{0 * BLOCK_SIZE + 1 * FRAGMENT, FRAGMENT, 2 * BLOCK_SIZE + 3 * FRAGMENT},
										{0 * BLOCK_SIZE + 2 * FRAGMENT, FRAGMENT, 3 * BLOCK_SIZE + 1 * FRAGMENT},
										{0 * BLOCK_SIZE + 3 * FRAGMENT, FRAGMENT, 0 * BLOCK_SIZE + 0 * FRAGMENT},
										{1 * BLOCK_SIZE + 0 * FRAGMENT, FRAGMENT, 0 * BLOCK_SIZE + 1 * FRAGMENT},
										{1 * BLOCK_SIZE + 1 * FRAGMENT, FRAGMENT, 1 * BLOCK_SIZE + 1 * FRAGMENT},
										{1 * BLOCK_SIZE + 2 * FRAGMENT, FRAGMENT, 2 * BLOCK_SIZE + 0 * FRAGMENT},
										{1 * BLOCK_SIZE + 3 * FRAGMENT, FRAGMENT, 3 * BLOCK_SIZE + 0 * FRAGMENT},
										{2 * BLOCK_SIZE + 0 * FRAGMENT, FRAGMENT, 3 * BLOCK_SIZE + 2 * FRAGMENT},
										{2 * BLOCK_SIZE + 1 * FRAGMENT, FRAGMENT, 0 * BLOCK_SIZE + 3 * FRAGMENT},
										{2 * BLOCK_SIZE + 2 * FRAGMENT, FRAGMENT, 1 * BLOCK_SIZE + 0 * FRAGMENT},
										{2 * BLOCK_SIZE + 3 * FRAGMENT, FRAGMENT, 2 * BLOCK_SIZE + 1 * FRAGMENT},
										{3 * BLOCK_SIZE + 1 * FRAGMENT, FRAGMENT, 3 * BLOCK_SIZE + 3 * FRAGMENT},
										{3 * BLOCK_SIZE + 2 * FRAGMENT, FRAGMENT, 0 * BLOCK_SIZE + 2 * FRAGMENT},
										{3 * BLOCK_SIZE + 2 * FRAGMENT, FRAGMENT, 1 * BLOCK_SIZE + 2 * FRAGMENT},
										{3 * BLOCK_SIZE + 3 * FRAGMENT, FRAGMENT, 2 * BLOCK_SIZE + 2 * FRAGMENT}};

	const vector<Command> expected = {{REBASE, 0x0, 0x3},
			//Exchange [0] and [1]
			{LOAD_AND_FLUSH, 0x1000},
			{COPY, TMP_BUF, 0x400, 0x400, 0x1000},
			{CHAINED_COPY, TMP_BUF, 0xc00, 0x400},
			{USE_BLOCK, 0x0},
			{CHAINED_COPY, 0x0, 0x0, 0x400},
			{CHAINED_COPY, 0x0, 0x800, 0x400},
			{COPY, 0x0, 0xc00, 0x400, TMP_BUF, 0x400},
			{COPY, 0x0, 0x400, 0x400, TMP_BUF, 0xc00},
			{FLUSH_AND_PARTIAL_COMMIT, 0x0, BLOCK_SIZE},
			{RELEASE_BLOCK},

			//Exchange [2] and [3]
			{COPY, 0x3000, 0x400, 0xc00, TMP_BUF},
			{FLUSH_AND_PARTIAL_COMMIT, 0x3000, 0x400},
			{USE_BLOCK, 0x2000},
			{CHAINED_COPY, 0x2000, 0x0, 0x400},
			{CHAINED_COPY, 0x2000, 0x800, 0x400},

			//Merger with [0] and [2] exchange
			{COPY, 0x2000, 0x400, 0x400, TMP_BUF},
			{COPY, 0x2000, 0xc00, 0x400, TMP_BUF, 0xc00},
			{ERASE, 0x2000},
			{USE_BLOCK, 0x0},
			{COPY, 0x0, 0x800, 0x400, 0x2000},
			{CHAINED_COPY, TMP_BUF, 0xc00, 0x400},
			{CHAINED_COPY, TMP_BUF, 0x800, 0x400},
			{CHAINED_COPY, 0x0, 0xc00, 0x400},

			//Write [0]
			{COPY, 0x0, 0x0, 0x800, TMP_BUF, 0x800},
			{ERASE, 0x0},
			{COPY, TMP_BUF, 0xc00, 0x400, 0x0},
			{CHAINED_COPY, TMP_BUF, 0x800, 0x400},
			{CHAINED_COPY, TMP_BUF, 0x400, 0x400},
			{CHAINED_COPY, TMP_BUF, 0x0, 0x400},
			{RELEASE_BLOCK},

			//Exchange [1] and [3]
			{LOAD_AND_FLUSH, 0x1000},
			{COPY, 0x3000, 0x800, 0x400, 0x1000},
			{CHAINED_COPY, TMP_BUF, 0x0, 0x400},
			{CHAINED_COPY, 0x0, 0x800, 0x400},
			{CHAINED_COPY, TMP_BUF, 0x800, 0x400},
			{USE_BLOCK, 0x3000},
			{COPY, 0x3000, 0x0, 0x400, TMP_BUF},
			{COPY, 0x3000, 0x400, 0x400, TMP_BUF, 0x800},
			{ERASE, 0x3000},
			{COPY, TMP_BUF, 0x400, 0x400, 0x3000},
			{CHAINED_COPY, TMP_BUF, 0xc00, 0x400},
			{CHAINED_COPY, TMP_BUF, 0x800, 0x400},
			{CHAINED_COPY, TMP_BUF, 0x0, 0x400}};

	vector<PublicCommand> output;
	schedule(input, output);
	return validateStaticResults(output, expected);
}

void performStaticTests()
{
	bool output = true;

	output &= firstPassTest();
	output &= secondPassTest();
	output &= thirdPassSimpleChain();
	output &= thirdPassTestWithFullRecovery();
	output &= thirdPassTestWithExternalReference();
	output &= forthPassTest();
	output &= forthPassTestWithCompetitiveRead();
	output &= forthPassTestWithHarderCompetitiveRead();
	output &= forthPassTestWithCompetitiveReadOnReusedSpace();

	if(output)
	{
#ifndef VERBOSE_STATIC_TESTS
		cout << "Static code generation tests successful" << endl;
#endif
	}
}

#if 0
int main()
{
	performStaticTests();

	runDynamicTestWithFiles("dynamic_test_files/old.txt", "dynamic_test_files/new.txt");
	runDynamicTestWithFiles("dynamic_test_files/old2.txt", "dynamic_test_files/new2.txt");
	runDynamicTestWithFiles("dynamic_test_files/blinkyv3.bin", "dynamic_test_files/blinkyv5.bin");
}
#endif
