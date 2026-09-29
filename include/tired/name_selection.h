#ifndef TIRED_NAME_SELECTION_H
#define TIRED_NAME_SELECTION_H
#include "tired/collision.h"
typedef struct TiredNameSelection TiredNameSelection;
/* Read-only selection state. Caller discovers all manager load paths, adds the
 * destination directory, and supplies the complete pending transaction inventory.
 * Lists are copied. Automatic bases are limited to 80 bytes; explicit bases 200.
 * No arbitrary suffix search limit: callers impose a deadline and cancellation. */
bool tired_name_selection_start(const TiredText *base, bool explicit_name,
                                const TiredTextList *directories,
                                const TiredTextList *pending_names, TiredNameSelection **selection,
                                TiredError *error);
/* Borrowed full name to query with the selected, authenticated manager. */
const TiredText *tired_name_selection_candidate(const TiredNameSelection *selection);
/* Consume a successful exact-name manager observation and inspect the filesystem.
 * A collision advances automatic names by one; explicit collisions fail.
 * Errors preserve selection state so callers can display/retry or cancel.
 * Once available is true, the candidate is a tentative proposal, not a reservation.
 * Commit must repeat discovery under the mutation lock. */
bool tired_name_selection_observe(TiredNameSelection *selection,
                                  const TiredUnitQueryResult *manager, bool *available,
                                  TiredError *error);
void tired_name_selection_destroy(TiredNameSelection *selection);
#endif
