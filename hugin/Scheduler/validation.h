//
// Created by Emlie-Hugo Spir on 5/14/18.
//

#ifndef HERMES_VALIDATION_H
#define HERMES_VALIDATION_H

bool executeBSDiffPatch(const SchedulerPatch & commands, uint8_t * flash, size_t flashLength);
bool validateSchedulerPatch(const uint8_t * original, size_t originalLength, const uint8_t * newer, size_t newLength, const SchedulerPatch & patch);

void generateVerificationRangesPrePatch(SchedulerPatch &patch, size_t initialOffset);
void generateVerificationRangesPostPatch(SchedulerPatch & patch, size_t initialOffset, const size_t fileLength);
void computeExpectedHashForRanges(std::vector<VerificationRange> &ranges, const uint8_t * data, size_t dataLength);

#endif //HERMES_VALIDATION_H
