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

#ifndef HERMES_VALIDATION_H
#define HERMES_VALIDATION_H

bool validateHeader(const UpdateHeader * header);
bool validateImage(const UpdateHeader * header);
bool validateExtraValidation(const UpdateHashRequest * request);

#endif //HERMES_VALIDATION_H
