#ifndef TIRED_DOCTOR_H
#define TIRED_DOCTOR_H
#include "tired/cli.h"
bool tired_doctor_command(const TiredRequest *request, TiredText *output, TiredStatus *status,
                          TiredError *error);
#endif
