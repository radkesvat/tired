#ifndef TIRED_MANIFEST_STORAGE_H
#define TIRED_MANIFEST_STORAGE_H
#include "tired/file_manifest.h"
#include "tired/publication.h"
/* Publish immutable files.json in a private transaction directory under the scope
 * lock, before appending prepare/completed to its journal. Never replace an entry.
 * Return the publication handle even on failure for durability retry/discard.
 * Caller owns scope/UUID directory selection and artifact preparation. */
bool tired_manifest_publish(TiredDirectory *directory, const TiredOperationLock *lock,
                            const TiredFileManifest *manifest, TiredPublication **publication,
                            TiredError *error);
/* Read files.json and journal/ from the same private transaction directory and
 * require an exact prepared-record match. Empty or staged journals cannot supply
 * a complete binding. Atomic owned output; no writes or live-state reconciliation.
 * Observation only: controllers must revalidate under the scope lock. */
bool tired_manifest_load(TiredDirectory *directory, TiredFileManifest *manifest, TiredError *error);
#endif
