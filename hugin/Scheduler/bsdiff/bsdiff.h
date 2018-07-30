//
// Created by Emile-Hugo Spir on 3/30/18.
//

#ifndef SCHEDULER_BSDIFF_H
#define SCHEDULER_BSDIFF_H

#ifdef BSDIFF_PRIVATE
extern "C"
{
	void qsufsort(off_t *index, off_t *value, const uint8_t *old, size_t oldSize);
	size_t search(off_t *index, const uint8_t *old, size_t oldSize, const uint8_t *newer, size_t newSize, size_t st, size_t en, size_t *matchPos);
	void offtout(uint32_t x, uint8_t *buf);
}
#endif

extern "C" uint8_t * readFile(const char * file, size_t * fileSize);

#ifdef HERMES_PUBLIC_COMMAND_H

	struct BSDiffPatch
	{
		size_t oldDataAddress;
		size_t lengthDelta;
		size_t lengthExtra;

		uint8_t *deltaData;
		uint8_t *extraData;

		BSDiffPatch(const size_t & oldDataAddress, const size_t & lengthDelta, uint8_t *deltaData, const size_t & lengthExtra, uint8_t * extraData)
				: oldDataAddress(oldDataAddress), lengthDelta(lengthDelta), lengthExtra(lengthExtra), deltaData(deltaData), extraData(extraData) {}
	};

	void bsdiff(const char * oldFile, const char * newFile, std::vector<BSDiffPatch> & patch);
	void bsdiff(const uint8_t * old, size_t oldSize, const uint8_t * newer, size_t newSize, std::vector<BSDiffPatch> & patch);
	bool writeBSDiff(const SchedulerPatch & patch, void * output);

	bool validateBSDiff(const uint8_t * original, size_t originalLength, const uint8_t * newer, size_t newLength, const std::vector<BSDiffPatch> & patch, size_t earlySkip);
#endif

#endif //SCHEDULER_BSDIFF_H
