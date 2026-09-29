#include "tired/file_reconciliation.h"
#include "tired/io.h"
#include "tired/private_file.h"
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
int main(void)
{
    int result = 1;
    char fixture[] = "reconcile-test-XXXXXX";
    char *created = NULL, *cwd = getcwd(NULL, 0);
    const char *record_name = "fedcba98-7654-4321-abcd-fedcba987654.json";
    TiredText path = {0};
    TiredLayout layout = {.user_scope = true};
    TiredDirectory *directory = NULL;
    TiredFileReconciliation observed = {0};
    TiredError error = {0};
    TiredFileChange files[2] = {0};
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
    files[0].target.role = TIRED_FILE_TARGET_UNIT;
    files[0].target.unit_name = manifest.prepared.unit_name;
    files[1].target.role = TIRED_FILE_TARGET_RECORD;
    for (size_t i = 0; i < 2; ++i)
    {
        memcpy(files[i].target.service_uuid, manifest.prepared.service_uuid, 37);
        memcpy(files[i].staging_uuid, manifest.prepared.transaction_uuid, 37);
        files[i].after = (TiredFileFingerprint){
            .exists = true,
            .mode = i == 0 ? 0644 : 0600,
            .sha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"};
    }
    CHECK(cwd != NULL && (created = mkdtemp(fixture)) != NULL);
    TiredText base = {.data = cwd, .length = strlen(cwd)};
    CHECK(tired_path_absolute(&base, created, strlen(created), &path, &error));
    CHECK(tired_directory_open(path.data, getuid(), true, &directory, &error));
    CHECK(tired_text_set(&layout.paths[TIRED_PATH_UNITS], path.data, path.length, 4096, &error));
    CHECK(tired_text_set(&layout.paths[TIRED_PATH_RECORDS], path.data, path.length, 4096, &error));
    CHECK(tired_file_reconcile(&layout, &manifest, &observed, &error));
    CHECK(observed.complete && !observed.foreign && observed.count == 2 &&
          observed.files[0].state == TIRED_FILE_BEFORE &&
          observed.files[1].state == TIRED_FILE_BEFORE);
    CHECK(tired_private_file_create(directory, "relay.service", "abc", 3, &error));
    CHECK(tired_private_file_create(directory, record_name, "record", 6, &error));
    int fd = tired_directory_fd(directory);
    CHECK(fchmodat(fd, "relay.service", 0644, 0) == 0);
    CHECK(tired_file_fingerprint(directory, "relay.service", 100, &files[0].after, &error));
    CHECK(tired_file_fingerprint(directory, record_name, 100, &files[1].after, &error));
    CHECK(tired_file_reconcile(&layout, &manifest, &observed, &error));
    CHECK(observed.complete && !observed.foreign && observed.files[0].state == TIRED_FILE_AFTER &&
          observed.files[1].state == TIRED_FILE_AFTER);
    CHECK(fchmodat(fd, "relay.service", 0600, 0) == 0);
    CHECK(tired_file_reconcile(&layout, &manifest, &observed, &error));
    CHECK(observed.complete && observed.foreign && observed.files[0].state == TIRED_FILE_FOREIGN);
    CHECK(unlinkat(fd, "relay.service", 0) == 0 &&
          symlinkat(record_name, fd, "relay.service") == 0);
    CHECK(tired_file_reconcile(&layout, &manifest, &observed, &error));
    CHECK(!observed.complete && observed.files[0].state == TIRED_FILE_UNKNOWN &&
          observed.files[0].error.status != TIRED_OK &&
          observed.files[1].state == TIRED_FILE_AFTER);
    CHECK(tired_path_absolute(&path, "missing", 7, &layout.paths[TIRED_PATH_UNITS], &error));
    CHECK(tired_file_reconcile(&layout, &manifest, &observed, &error));
    CHECK(observed.complete && observed.files[0].state == TIRED_FILE_BEFORE);
    layout.user_scope = false;
    CHECK(!tired_file_reconcile(&layout, &manifest, &observed, &error));
    CHECK(error.status == TIRED_CONFLICT && observed.count == 2 && observed.complete);
    result = 0;
cleanup:
    if (directory != NULL)
    {
        (void)unlinkat(tired_directory_fd(directory), "relay.service", 0);
        (void)unlinkat(tired_directory_fd(directory), record_name, 0);
    }
    tired_directory_destroy(directory);
    tired_file_reconciliation_destroy(&observed);
    tired_layout_destroy(&layout);
    tired_text_destroy(&path);
    if (created != NULL)
        (void)rmdir(created);
    free(cwd);
    return result;
}
