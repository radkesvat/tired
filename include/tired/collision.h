#ifndef TIRED_COLLISION_H
#define TIRED_COLLISION_H
#include "tired/unit_query.h"
typedef enum
{
    TIRED_COLLISION_NONE,
    TIRED_COLLISION_MANAGER_FILE,
    TIRED_COLLISION_MANAGER_OBJECT,
    TIRED_COLLISION_TRANSACTION,
    TIRED_COLLISION_UNIT_ENTRY,
    TIRED_COLLISION_DROP_IN
} TiredCollisionKind;
typedef struct
{
    TiredCollisionKind kind;
    TiredText path;
} TiredCollision;
void tired_collision_destroy(TiredCollision *collision);
/* Combine a successful query for this exact unit with a validated pending-name
 * inventory and all relevant load/destination directories. At least one directory
 * is required. Reads only: any unit/drop-in entry reserves the name, even dangling
 * links and nonregular files. Directory symlinks follow the manager's search path.
 * Missing locations are empty; other access failures never mean available.
 * Atomic output. NONE is a tentative observation, never a reservation or approval. */
bool tired_collision_check(const TiredText *unit_name, const TiredUnitQueryResult *manager,
                           const TiredTextList *directories, const TiredTextList *pending_names,
                           TiredCollision *collision, TiredError *error);
#endif
