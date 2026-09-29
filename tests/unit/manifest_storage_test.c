#include "tired/io.h"
#include "tired/manifest_storage.h"
#include "tired/transaction_journal.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
    char fixture[] = "manifest-storage-XXXXXX";
    char *created = NULL, *cwd = getcwd(NULL, 0);
    TiredText path = {0};
    TiredDirectory *root = NULL, *journal = NULL;
    TiredOperationLock *lock = NULL;
    TiredPublication *publication = NULL, *staging = NULL;
    TiredFileManifest
        loaded = {0},
        source = {
            .prepared = {.transaction_uuid = "01234567-89ab-4cde-8fab-0123456789ab",
                         .service_uuid = "fedcba98-7654-4321-abcd-fedcba987654",
                         .approved_sha256 =
                             "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                         .unit_name = {.data = "relay.service", .length = 13},
                         .operation = TIRED_TRANSACTION_START,
                         .sequence = 1,
                         .action = TIRED_ACTION_PREPARE,
                         .state = TIRED_ACTION_COMPLETED}};
    TiredError error = {0};
    CHECK(cwd != NULL && (created = mkdtemp(fixture)) != NULL);
    TiredText base = {.data = cwd, .length = strlen(cwd)};
    CHECK(tired_path_absolute(&base, created, strlen(created), &path, &error));
    CHECK(tired_directory_open(path.data, getuid(), true, &root, &error));
    CHECK(tired_directory_child(root, "journal", true, true, &journal, &error));
    CHECK(tired_operation_lock_acquire(root, &lock, &error));
    CHECK(!tired_manifest_load(root, &loaded, &error) && error.status == TIRED_NOT_FOUND);
    CHECK(tired_manifest_publish(root, lock, &source, &publication, &error));
    CHECK(tired_publication_durable(publication));
    tired_publication_destroy(publication);
    publication = NULL;
    CHECK(!tired_manifest_load(root, &loaded, &error));
    CHECK(error.status == TIRED_RECOVERY_REQUIRED && loaded.prepared.unit_name.data == NULL);
    CHECK(tired_transaction_journal_append(journal, lock, &source.prepared, &publication, &error));
    tired_publication_destroy(publication);
    publication = NULL;
    CHECK(tired_manifest_load(root, &loaded, &error));
    CHECK(strcmp(loaded.prepared.transaction_uuid, source.prepared.transaction_uuid) == 0);
    CHECK(!tired_manifest_publish(root, lock, &source, &publication, &error));
    CHECK(!tired_publication_published(publication));
    CHECK(tired_publication_discard(publication, &error));
    tired_publication_destroy(publication);
    publication = NULL;
    CHECK(tired_publication_prepare(journal, "0002.json", "partial", 7, 0600, &staging, &error));
    CHECK(!tired_manifest_load(root, &loaded, &error));
    CHECK(error.status == TIRED_RECOVERY_REQUIRED && loaded.prepared.sequence == 1);
    CHECK(tired_publication_discard(staging, &error));
    tired_publication_destroy(staging);
    staging = NULL;
    int parent = tired_directory_fd(root), fd = tired_directory_fd(journal);
    CHECK(fchmodat(parent, "files.json", 0644, 0) == 0);
    CHECK(!tired_manifest_load(root, &loaded, &error) && error.status == TIRED_CONFLICT);
    CHECK(fchmodat(parent, "files.json", 0600, 0) == 0);
    CHECK(renameat(parent, "files.json", parent, "saved") == 0);
    CHECK(symlinkat("saved", parent, "files.json") == 0);
    CHECK(!tired_manifest_load(root, &loaded, &error));
    CHECK(unlinkat(parent, "files.json", 0) == 0);
    CHECK(renameat(parent, "saved", parent, "files.json") == 0);
    CHECK(unlinkat(fd, "0001.json", 0) == 0);
    source.prepared.approved_sha256[0] = 'a';
    CHECK(tired_transaction_journal_append(journal, lock, &source.prepared, &publication, &error));
    tired_publication_destroy(publication);
    publication = NULL;
    CHECK(!tired_manifest_load(root, &loaded, &error));
    CHECK(strcmp(error.code, "manifest-journal-mismatch") == 0 &&
          loaded.prepared.approved_sha256[0] == '0');
    result = 0;
cleanup:
    if (publication != NULL)
        (void)tired_publication_discard(publication, &error);
    if (staging != NULL)
        (void)tired_publication_discard(staging, &error);
    tired_publication_destroy(publication);
    tired_publication_destroy(staging);
    tired_file_manifest_destroy(&loaded);
    tired_operation_lock_destroy(lock);
    if (journal != NULL)
        (void)unlinkat(tired_directory_fd(journal), "0001.json", 0);
    tired_directory_destroy(journal);
    if (root != NULL)
    {
        int fd = tired_directory_fd(root);
        (void)unlinkat(fd, "files.json", 0);
        (void)unlinkat(fd, "saved", 0);
        (void)unlinkat(fd, "operation.lock", 0);
        (void)unlinkat(fd, "journal", AT_REMOVEDIR);
    }
    tired_directory_destroy(root);
    if (created != NULL)
        (void)rmdir(created);
    tired_text_destroy(&path);
    free(cwd);
    return result;
}
