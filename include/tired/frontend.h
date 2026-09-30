#ifndef TIRED_FRONTEND_H
#define TIRED_FRONTEND_H
#include "tired/mutation.h"
/* Full creation/live planning and lifecycle dispatch, owns no passed values. */
bool tired_frontend_command(const TiredRequest *request, const char *profiles, TiredText *output,
                            TiredStatus *status, TiredError *error);
/* Rebuild derived record paths, resolved identity/evidence and approval digest
 * after a typed edit. No workload execution or publication. */
bool tired_mutation_refresh(TiredMutation *mutation, const TiredLayout *layout, TiredError *error);
bool tired_edit_inputs(const TiredRequest *request, TiredMutation *mutation,
                       const TiredBackend *backend, const TiredSettings *settings,
                       const char *bundled_directory, TiredError *error);
bool tired_frontend_dashboard(const TiredRequest *request, const char *profiles, TiredText *output,
                              TiredStatus *status, TiredError *error);
#endif
