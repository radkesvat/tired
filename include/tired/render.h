#ifndef TIRED_RENDER_H
#define TIRED_RENDER_H
#include "tired/environment.h"
#include "tired/model.h"

/* Render a fully captured proposal with resolved defaults. argv includes the
 * original argv[0], which is replaced by executable. uuid is canonical lowercase.
 * managed_environment is NULL or an absolute private revision path. External
 * files are emitted first. Output is owned and unchanged on failure.
 * No files or manager state are touched; runtime validation is still required. */
bool tired_render_unit(const TiredServiceSpec *spec, const char *uuid,
                       const TiredText *managed_environment, const TiredCredentials *credentials,
                       TiredText *output, TiredError *error);
#endif
