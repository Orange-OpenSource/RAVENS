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

#ifndef RAVENS_VALIDATION_H
#define RAVENS_VALIDATION_H

bool executeBSDiffPatch(const SchedulerPatch & commands, uint8_t * flash, size_t flashLength);
bool validateSchedulerPatch(const uint8_t * original, size_t originalLength, const uint8_t * newer, size_t newLength, const SchedulerPatch & patch);

void generateVerificationRangesPrePatch(SchedulerPatch &patch, size_t initialOffset);
void generateVerificationRangesPostPatch(SchedulerPatch & patch, size_t initialOffset, const size_t fileLength);
void computeExpectedHashForRanges(std::vector<VerificationRange> &ranges, const uint8_t * data, size_t dataLength);

#endif //RAVENS_VALIDATION_H
