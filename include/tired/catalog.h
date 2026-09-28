#ifndef TIRED_CATALOG_H
#define TIRED_CATALOG_H
#include "tired/profile.h"
#include <sys/types.h>

typedef enum
{
    TIRED_PROFILE_BUNDLED,
    TIRED_PROFILE_ADMIN,
    TIRED_PROFILE_USER
} TiredProfileOrigin;
typedef struct
{
    TiredProfile profile;
    TiredText path;
    TiredProfileOrigin origin;
    uid_t trusted_owner;
    char digest[65];
} TiredProfileEntry;
typedef struct
{
    TiredProfileEntry *items;
    size_t count;
} TiredProfileCatalog;
/* Scan only this absolute directory, no recursion/search of cwd. Every ancestor
 * must be owned by root or trusted_owner and not group/other-writable. Symlinks
 * are rejected at all components; profile files additionally require one link.
 * Missing directories are allowed only when optional is true. Appends atomically
 * and rejects duplicate IDs rather than silently replacing. No borrowed entry
 * pointers may be retained across an append attempt. */
bool tired_catalog_add_directory(TiredProfileCatalog *catalog, const char *path,
                                 TiredProfileOrigin origin, uid_t trusted_owner, bool optional,
                                 TiredError *error);
/* Explicit ID or auto. Ambiguous auto selection returns NULL with count > 1;
 * generic is a fallback only. User-origin entries never participate in system scope. */
bool tired_catalog_select(const TiredProfileCatalog *catalog, const TiredText *executable,
                          const char *selection, bool user_scope, const TiredProfileEntry **entry,
                          size_t *match_count, TiredError *error);
void tired_catalog_destroy(TiredProfileCatalog *catalog);
#endif
