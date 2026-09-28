#ifndef TIRED_PROPOSAL_H
#define TIRED_PROPOSAL_H
#include "tired/capture.h"
#include "tired/identity.h"
#include "tired/model.h"
#include "tired/name.h"

/* Construct a generic proposal from captured context. Account lookup uses the
 * captured real UID; no sudo environment hint is treated as authority. No live
 * manager, filesystem mutation, or target execution. Outputs are replaced only
 * on success and must be initialized to zero. Names remain tentative. */
bool tired_proposal_generic(const TiredInvocation *invocation, bool user_scope,
                            TiredServiceSpec *spec, TiredAccount *invoking_account,
                            TiredNameBasis *name_basis, TiredError *error);
#endif
