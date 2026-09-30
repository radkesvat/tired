#include "tired/file_fingerprint.h"
#include "tired/helper.h"
#include "tired/io.h"
#include "tired/name.h"
#include "tired/private_file.h"
#include "tired/service_files.h"
#include "tired/service_inventory.h"
#include "tired/service_record_storage.h"
#include <stdio.h>
#include <string.h>
bool tired_helper_record(const char *name, bool hydrate, uid_t actor, TiredText *output,
                         TiredError *error)
{
    TiredLayout layout = {0};
    TiredMutation mutation = {.operation = TIRED_TRANSACTION_EDIT, .actor_uid = actor};
    TiredServiceInventory inventory = {0};
    TiredServiceFiles files = {0};
    TiredText base = {0}, unit = {0};
    TiredDirectory *directory = NULL;
    const TiredServiceMetadata *metadata = NULL;
    TiredFileFingerprint fingerprint = {0};
    bool ok =
        tired_name_explicit(name, strlen(name), &base, error) &&
        tired_name_candidate(&base, 1, &unit, error) &&
        tired_layout_discover(false, &layout, error) &&
        tired_service_inventory_load(&layout, &inventory, error) &&
        tired_service_inventory_find(&inventory, unit.data, &metadata, error) &&
        tired_service_record_load(&layout, metadata->service_uuid, &mutation.proposed, error) &&
        tired_service_files_inspect(&layout, &mutation.proposed, &files, error);
    if (ok && (!files.unit.actual.exists || !files.unit.owner_matches || !files.unit.mode_matches ||
               !files.unit.marker_matches ||
               (mutation.proposed.has_environment &&
                files.environment.state != TIRED_SERVICE_FILE_MATCH)))
        ok = tired_error_set(error, TIRED_CONFLICT, "managed-ownership",
                             "The requested service has unsafe or missing ownership evidence.", 0);
    char file[42];
    if (ok)
    {
        (void)snprintf(file, sizeof(file), "%s.json", metadata->service_uuid);
        ok = tired_directory_open(layout.paths[TIRED_PATH_RECORDS].data, 0, true, &directory,
                                  error) &&
             tired_file_fingerprint(directory, file, TIRED_SERVICE_RECORD_LIMIT, &fingerprint,
                                    error) &&
             tired_text_set(&mutation.expected_record_sha256, fingerprint.sha256, 64, 64, error) &&
             tired_text_set(&mutation.expected_unit_sha256, files.unit.actual.sha256, 64, 64,
                            error) &&
             (!hydrate || tired_service_record_hydrate(&mutation.proposed, &layout, error)) &&
             tired_mutation_digest(&mutation, mutation.proposed.review.approved_sha256, error) &&
             tired_mutation_encode(&mutation, output, error);
    }
    tired_mutation_destroy(&mutation);
    tired_service_inventory_destroy(&inventory);
    tired_directory_destroy(directory);
    tired_text_destroy(&base);
    tired_text_destroy(&unit);
    tired_layout_destroy(&layout);
    return ok;
}
