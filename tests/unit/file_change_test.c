#include "tired/file_change.h"
#include "tired/io.h"
#include "tired/private_file.h"
#include "tired/publication.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
static bool stage(TiredDirectory *directory, const char *name, const char *bytes,
                  TiredFileChange *change, char saved[64], TiredError *error)
{
    TiredPublication *publication = NULL;
    bool ok = tired_publication_prepare(directory, name, bytes, strlen(bytes),
                                        change->target.role == TIRED_FILE_TARGET_UNIT ? 0644 : 0600,
                                        &publication, error);
    if (publication != NULL)
    {
        (void)snprintf(saved, 64, "%s", tired_publication_temporary_name(publication));
        memcpy(change->staging_uuid, saved + 7, 36);
        change->staging_uuid[36] = '\0';
        if (ok)
            ok = tired_file_fingerprint(directory, saved, 100, &change->after, error);
    }
    tired_publication_destroy(publication);
    return ok;
}
int main(void)
{
    int result = 1;
    char fixture[] = "file-change-XXXXXX", staging[4][64] = {{0}};
    char *created = NULL, *cwd = getcwd(NULL, 0);
    const char *names[] = {"relay.service", "fedcba98-7654-4321-abcd-fedcba987654.json"};
    TiredText path = {0};
    TiredLayout layout = {.user_scope = true};
    TiredDirectory *root = NULL;
    TiredOperationLock *lock = NULL;
    TiredError error = {0};
    TiredFileChange files[2] = {0};
    TiredFileFingerprint originals[2] = {0};
    TiredFileChangeResult applied = {0};
    TiredFileManifest manifest = {
        .files = files,
        .count = 2,
        .prepared = {.transaction_uuid = "01234567-89ab-4cde-8fab-0123456789ab",
                     .service_uuid = "fedcba98-7654-4321-abcd-fedcba987654",
                     .approved_sha256 =
                         "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                     .unit_name = {.data = "relay.service", .length = 13},
                     .user_scope = true,
                     .operation = TIRED_TRANSACTION_CREATE,
                     .sequence = 1,
                     .action = TIRED_ACTION_PREPARE,
                     .state = TIRED_ACTION_COMPLETED}};
    CHECK(cwd != NULL && (created = mkdtemp(fixture)) != NULL);
    TiredText base = {.data = cwd, .length = strlen(cwd)};
    CHECK(tired_path_absolute(&base, created, strlen(created), &path, &error));
    CHECK(tired_directory_open(path.data, getuid(), true, &root, &error));
    CHECK(tired_operation_lock_acquire(root, &lock, &error));
    CHECK(tired_text_set(&layout.paths[TIRED_PATH_UNITS], path.data, path.length, 4096, &error));
    CHECK(tired_text_set(&layout.paths[TIRED_PATH_RECORDS], path.data, path.length, 4096, &error));
    files[0].target.role = TIRED_FILE_TARGET_UNIT;
    files[0].target.unit_name = manifest.prepared.unit_name;
    files[1].target.role = TIRED_FILE_TARGET_RECORD;
    for (size_t i = 0; i < 2; ++i)
    {
        memcpy(files[i].target.service_uuid, manifest.prepared.service_uuid, 37);
        CHECK(stage(root, names[i], "original", &files[i], staging[i], &error));
        originals[i] = files[i].after;
    }
    CHECK(tired_file_change_apply(&layout, &manifest, 0, true, lock, &applied, &error));
    CHECK(applied.reached && applied.durable);
    TiredFileChange ordered_files[3] = {files[1], files[0], files[1]};
    ordered_files[2].target.role = TIRED_FILE_TARGET_ENVIRONMENT;
    memcpy(ordered_files[2].target.revision_uuid, manifest.prepared.transaction_uuid, 37);
    TiredFileManifest order_manifest = manifest;
    order_manifest.files = ordered_files;
    order_manifest.count = 3;
    TiredFileOrder order = {0};
    CHECK(tired_file_change_order(&order_manifest, false, &order, &error));
    CHECK(order.count == 3 && order.indices[0] == 2 && order.indices[1] == 1 &&
          order.indices[2] == 0);
    CHECK(tired_file_change_order(&order_manifest, true, &order, &error));
    CHECK(order.indices[0] == 0 && order.indices[1] == 1 && order.indices[2] == 2);
    order_manifest.prepared.operation = TIRED_TRANSACTION_REMOVE;
    for (size_t i = 0; i < 3; ++i)
    {
        ordered_files[i].before = ordered_files[i].after;
        ordered_files[i].after = (TiredFileFingerprint){0};
        memcpy(ordered_files[i].rollback_uuid, ordered_files[i].staging_uuid, 37);
        ordered_files[i].staging_uuid[0] = '\0';
    }
    CHECK(tired_file_change_order(&order_manifest, false, &order, &error));
    CHECK(order.indices[0] == 1 && order.indices[1] == 0 && order.indices[2] == 2);
    TiredFilePhaseResult phase = {0};
    CHECK(tired_private_file_create(root, names[1], "foreign", 7, &error));
    CHECK(!tired_file_phase_apply(&layout, &manifest, false, lock, &phase, &error));
    CHECK(phase.completed == 1 && phase.failed_index == 1 && !phase.reached);
    CHECK(unlinkat(tired_directory_fd(root), names[1], 0) == 0);
    CHECK(tired_file_phase_apply(&layout, &manifest, false, lock, &phase, &error));
    CHECK(phase.completed == 2 && phase.failed_index == SIZE_MAX);
    CHECK(tired_file_phase_apply(&layout, &manifest, false, lock, &phase, &error));
    manifest.prepared.operation = TIRED_TRANSACTION_EDIT;
    for (size_t i = 0; i < 2; ++i)
    {
        files[i].before = originals[i];
        memcpy(files[i].rollback_uuid, manifest.prepared.transaction_uuid, 37);
        files[i].rollback_uuid[0] = (char)('a' + i);
        CHECK(stage(root, names[i], "edited", &files[i], staging[i + 2], &error));
    }
    CHECK(tired_file_phase_apply(&layout, &manifest, false, lock, &phase, &error));
    CHECK(tired_file_phase_apply(&layout, &manifest, true, lock, &phase, &error));
    CHECK(tired_file_phase_apply(&layout, &manifest, true, lock, &phase, &error));
    manifest.prepared.operation = TIRED_TRANSACTION_REMOVE;
    for (size_t i = 0; i < 2; ++i)
    {
        files[i].after = (TiredFileFingerprint){0};
        files[i].staging_uuid[0] = '\0';
    }
    for (size_t i = 0; i < 2; ++i)
    {
        CHECK(tired_file_change_apply(&layout, &manifest, i, false, lock, &applied, &error));
        CHECK(tired_file_change_apply(&layout, &manifest, i, false, lock, &applied, &error));
        CHECK(tired_file_change_apply(&layout, &manifest, i, true, lock, &applied, &error));
    }
    manifest.prepared.operation = TIRED_TRANSACTION_CREATE;
    for (size_t i = 0; i < 2; ++i)
    {
        files[i].before = (TiredFileFingerprint){0};
        files[i].after = originals[i];
        files[i].rollback_uuid[0] = '\0';
        memcpy(files[i].staging_uuid, staging[i] + 7, 36);
        files[i].staging_uuid[36] = '\0';
    }
    for (size_t i = 0; i < 2; ++i)
    {
        CHECK(tired_file_change_apply(&layout, &manifest, i, true, lock, &applied, &error));
        CHECK(tired_file_change_apply(&layout, &manifest, i, true, lock, &applied, &error));
    }
    CHECK(tired_private_file_create(root, names[0], "foreign", 7, &error));
    CHECK(!tired_file_change_apply(&layout, &manifest, 0, true, lock, &applied, &error));
    CHECK(error.status == TIRED_CONFLICT && !applied.reached);
    CHECK(!tired_file_change_apply(&layout, &manifest, 2, false, lock, &applied, &error));
    result = 0;
cleanup:
    tired_operation_lock_destroy(lock);
    if (root != NULL)
    {
        int fd = tired_directory_fd(root);
        for (size_t i = 0; i < 4; ++i)
        {
            if (staging[i][0] == '\0')
                continue;
            (void)unlinkat(fd, staging[i], 0);
            char retained[52];
            (void)snprintf(retained, sizeof(retained), "%.43s.removed", staging[i]);
            (void)unlinkat(fd, retained, 0);
        }
        (void)unlinkat(fd, names[0], 0);
        (void)unlinkat(fd, names[1], 0);
        (void)unlinkat(fd, ".tired-a1234567-89ab-4cde-8fab-0123456789ab.removed", 0);
        (void)unlinkat(fd, ".tired-b1234567-89ab-4cde-8fab-0123456789ab.removed", 0);
        (void)unlinkat(fd, "operation.lock", 0);
    }
    tired_directory_destroy(root);
    tired_layout_destroy(&layout);
    tired_text_destroy(&path);
    if (created != NULL)
        (void)rmdir(created);
    free(cwd);
    return result;
}
