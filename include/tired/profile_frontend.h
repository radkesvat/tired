#ifndef TIRED_PROFILE_FRONTEND_H
#define TIRED_PROFILE_FRONTEND_H
#include "tired/plan.h"
bool tired_profiles_discover(const char *bundled_directory, bool user_scope,
                             TiredProfileCatalog *catalog, TiredError *error);
bool tired_profiles_command(const TiredRequest *request, const char *bundled_directory,
                            TiredText *output, TiredError *error);
#endif
