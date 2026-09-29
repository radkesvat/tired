#ifndef TIRED_CONFIG_FRONTEND_H
#define TIRED_CONFIG_FRONTEND_H
#include "tired/cli.h"
#include "tired/settings.h"

/* Discover /etc/tired/config.json and the invoking user's XDG config, then apply
 * explicit presentation CLI choices. No manager access; output is atomic. */
bool tired_config_discover(const TiredRequest *request, TiredSettings *settings, TiredError *error);

/* Read-only command handling. Validation reads only the explicit file and merges
 * it over built-in defaults; it establishes no filesystem trust. Output is atomic. */
bool tired_config_command(const TiredRequest *request, TiredText *output, TiredError *error);
#endif
