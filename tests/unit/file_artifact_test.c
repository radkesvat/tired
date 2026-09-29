#include "tired/file_artifact.h"
#include "tired/file_backup.h"
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
    char fixture[] = "artifact-test-XXXXXX";
    char *created = NULL, *cwd = getcwd(NULL, 0);
    const char *staging = ".tired-01234567-89ab-4cde-8fab-0123456789ab.tmp";
    TiredText path = {0};
    TiredDirectory *root = NULL, *artifacts = NULL;
    TiredOperationLock *lock = NULL;
    TiredPublication *publication = NULL;
    TiredLayout layout = {.user_scope = true};
    TiredArtifactObservation observed = {0};
    TiredError error = {0};
    TiredFileChange change = {
        .target = {.role = TIRED_FILE_TARGET_UNIT,
                   .service_uuid = "fedcba98-7654-4321-abcd-fedcba987654",
                   .unit_name = {.data = "relay.service", .length = 13}},
        .staging_uuid = "01234567-89ab-4cde-8fab-0123456789ab",
        .rollback_uuid = "11234567-89ab-4cde-8fab-0123456789ab",
        .after = {.exists = true,
                  .mode = 0644,
                  .sha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                  .size = 3}};
    change.before = change.after;
    CHECK(cwd != NULL && (created = mkdtemp(fixture)) != NULL);
    TiredText base = {.data = cwd, .length = strlen(cwd)};
    CHECK(tired_path_absolute(&base, created, strlen(created), &path, &error));
    CHECK(tired_directory_open(path.data, getuid(), true, &root, &error));
    CHECK(tired_operation_lock_acquire(root, &lock, &error));
    CHECK(tired_text_set(&layout.paths[TIRED_PATH_UNITS], path.data, path.length, 4096, &error));
    CHECK(tired_file_artifact_observe(&layout, root, &change, false, 100, &observed, &error));
    CHECK(observed.state == TIRED_ARTIFACT_MISSING);
    CHECK(tired_file_artifact_observe(&layout, root, &change, true, 100, &observed, NULL));
    CHECK(tired_file_artifact_observe(&layout, root, &change, true, 100, &observed, &error));
    CHECK(observed.state == TIRED_ARTIFACT_MISSING);
    CHECK(tired_directory_child(root, "artifacts", true, true, &artifacts, &error));
    CHECK(tired_private_file_create(artifacts, change.rollback_uuid, "abc", 3, &error));
    CHECK(tired_file_artifact_observe(&layout, root, &change, true, 100, &observed, &error));
    CHECK(observed.state == TIRED_ARTIFACT_MATCH && observed.actual.inode != change.before.inode);
    int backup_fd = tired_directory_fd(artifacts), fd = tired_directory_fd(root);
    CHECK(fchmodat(backup_fd, change.rollback_uuid, 0644, 0) == 0);
    CHECK(tired_file_artifact_observe(&layout, root, &change, true, 100, &observed, &error));
    CHECK(observed.state == TIRED_ARTIFACT_DIFFERENT);
    CHECK(fchmodat(backup_fd, change.rollback_uuid, 0600, 0) == 0);
    change.before.sha256[0] = '0';
    CHECK(tired_file_artifact_observe(&layout, root, &change, true, 100, &observed, &error));
    CHECK(observed.state == TIRED_ARTIFACT_DIFFERENT);
    CHECK(tired_private_file_create(root, staging, "abc", 3, &error));
    CHECK(fchmodat(fd, staging, 0644, 0) == 0);
    CHECK(tired_file_fingerprint(root, staging, 100, &change.after, &error));
    CHECK(tired_file_artifact_observe(&layout, root, &change, false, 100, &observed, &error));
    CHECK(observed.state == TIRED_ARTIFACT_MATCH);
    CHECK(unlinkat(backup_fd, change.rollback_uuid, 0) == 0);
    change.before = change.after;
    CHECK(tired_file_backup(root, staging, &change.before, artifacts, change.rollback_uuid, lock,
                            &publication, &error));
    CHECK(tired_publication_durable(publication));
    tired_publication_destroy(publication);
    publication = NULL;
    CHECK(tired_file_artifact_observe(&layout, root, &change, true, 100, &observed, &error));
    CHECK(observed.state == TIRED_ARTIFACT_MATCH && observed.actual.mode == 0600);
    CHECK(!tired_file_backup(root, staging, &change.before, artifacts, change.rollback_uuid, lock,
                             &publication, &error));
    CHECK(!tired_publication_published(publication));
    CHECK(tired_publication_discard(publication, &error));
    tired_publication_destroy(publication);
    publication = NULL;
    ++change.before.inode;
    CHECK(!tired_file_backup(root, staging, &change.before, artifacts, change.rollback_uuid, lock,
                             &publication, &error));
    CHECK(error.status == TIRED_CONFLICT && publication == NULL);
    ++change.after.inode;
    CHECK(tired_file_artifact_observe(&layout, root, &change, false, 100, &observed, &error));
    CHECK(observed.state == TIRED_ARTIFACT_DIFFERENT);
    CHECK(!tired_file_artifact_observe(&layout, root, &change, false, 2, &observed, &error));
    CHECK(observed.state == TIRED_ARTIFACT_DIFFERENT);
    CHECK(unlinkat(fd, staging, 0) == 0 && symlinkat("artifacts", fd, staging) == 0);
    CHECK(!tired_file_artifact_observe(&layout, root, &change, false, 100, &observed, &error));
    CHECK(observed.state == TIRED_ARTIFACT_DIFFERENT);
    result = 0;
cleanup:
    if (publication != NULL)
        (void)tired_publication_discard(publication, &error);
    tired_publication_destroy(publication);
    tired_operation_lock_destroy(lock);
    if (artifacts != NULL)
        (void)unlinkat(tired_directory_fd(artifacts), change.rollback_uuid, 0);
    tired_directory_destroy(artifacts);
    if (root != NULL)
    {
        (void)unlinkat(tired_directory_fd(root), staging, 0);
        (void)unlinkat(tired_directory_fd(root), "operation.lock", 0);
        (void)unlinkat(tired_directory_fd(root), "artifacts", AT_REMOVEDIR);
    }
    tired_directory_destroy(root);
    tired_layout_destroy(&layout);
    tired_text_destroy(&path);
    if (created != NULL)
        (void)rmdir(created);
    free(cwd);
    return result;
}
