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

#ifndef HERMES_BSDIFF_H
#define HERMES_BSDIFF_H

bool applyDeltaPatch(const UpdateHeader * header, size_t currentIndex, size_t traceCounter, size_t previousCounter, bool dryRun);

#endif //HERMES_BSDIFF_H
