#ifndef TIRED_DIRECTORY_H
#define TIRED_DIRECTORY_H
#include "tired/value.h"
#include <sys/types.h>
typedef struct TiredDirectory TiredDirectory;
/* Open an existing normalized absolute path component by component, without
 * following links. Ancestors must belong to root or owner and forbid group/other
 * writes. A private final directory must belong to owner with mode exactly 0700.
 * No directories are created. Missing paths return TIRED_NOT_FOUND. */
bool tired_directory_open(const char *path, uid_t owner, bool private_directory,
                          TiredDirectory **directory, TiredError *error);
/* Open a single child of a validated directory. create permits mkdir(0700), only
 * as owner; existing children are never chmod/chown'ed. Newly created children
 * and their parent are fsynced. Failure can leave an empty private directory;
 * never automatically remove it by an unverified pathname. No recursive creation. */
bool tired_directory_child(TiredDirectory *parent, const char *name, bool create,
                           bool private_directory, TiredDirectory **directory, TiredError *error);
/* As above, but new directories may be explicitly public 0755 for administrator
 * configuration/profile discovery. Existing entries are never chmod'ed. */
bool tired_directory_child_mode(TiredDirectory *parent, const char *name, bool create,
                                bool private_directory, unsigned creation_mode,
                                TiredDirectory **directory, TiredError *error);
/* Recheck permissions/type and the immediate parent/name binding. A descriptor
 * pins an inode, not every ancestor's location; controllers must reopen/revalidate
 * the scope root before commit and hold their mutation lock. */
bool tired_directory_check(const TiredDirectory *directory, TiredError *error);
/* Borrowed descriptor for descriptor-relative storage operations; do not close. */
int tired_directory_fd(const TiredDirectory *directory);
void tired_directory_destroy(TiredDirectory *directory);
#endif
