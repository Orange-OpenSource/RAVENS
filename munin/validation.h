//
// Created by Emile-Hugo Spir on 4/25/18.
//

#ifndef HERMES_VALIDATION_H
#define HERMES_VALIDATION_H

bool validateHeader(const UpdateHeader * header);
bool validateImage(const UpdateHeader * header);
bool validateExtraValidation(const UpdateHashRequest * request);

#endif //HERMES_VALIDATION_H
