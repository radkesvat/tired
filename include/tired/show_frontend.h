#ifndef TIRED_SHOW_FRONTEND_H
#define TIRED_SHOW_FRONTEND_H
#include "tired/cli.h"
/* Saved model/provenance plus a stable read of the installed unit. No manager
 * mutation, adoption or regenerated unit substitute. --output creates a new
 * private file; sensitive export requires its explicit risk acknowledgment.
 * Otherwise output is redacted. Owned output/status are atomic on failure. */
bool tired_show_command(const TiredRequest *request, TiredText *output, TiredStatus *result,
                        TiredError *error);
#endif
