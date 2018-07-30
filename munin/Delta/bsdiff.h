//
// Created by Emile-Hugo Spir on 4/30/18.
//

#ifndef HERMES_BSDIFF_H
#define HERMES_BSDIFF_H

bool applyDeltaPatch(const UpdateHeader * header, size_t currentIndex, size_t traceCounter, size_t previousCounter, bool dryRun);

#endif //HERMES_BSDIFF_H
