#include "tired/io.h"
#include "tired/private_file.h"
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
    char fixture[] = "journal-test-XXXXXX";
    char *created = NULL, *cwd = getcwd(NULL, 0);
    TiredText path = {0}, encoded = {0};
    TiredDirectory *root = NULL, *directory = NULL;
    TiredOperationLock *lock = NULL;
    TiredPublication *publication = NULL, *staging = NULL;
    TiredTransactionJournal journal = {0};
    TiredError error = {0};
    TiredTransactionRecord record = {
        .transaction_uuid = "01234567-89ab-4cde-8fab-0123456789ab",
        .service_uuid = "fedcba98-7654-4321-abcd-fedcba987654",
        .approved_sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        .unit_name = {.data = "relay.service", .length = 13},
        .operation = TIRED_TRANSACTION_CREATE,
        .action = TIRED_ACTION_PREPARE,
        .state = TIRED_ACTION_COMPLETED,
        .sequence = 1};
    CHECK(cwd != NULL && (created = mkdtemp(fixture)) != NULL);
    TiredText base = {.data = cwd, .length = strlen(cwd)};
    CHECK(tired_path_absolute(&base, created, strlen(created), &path, &error));
    CHECK(tired_directory_open(path.data, getuid(), true, &root, &error));
    CHECK(tired_directory_child(root, "journal", true, true, &directory, &error));
    CHECK(tired_operation_lock_acquire(root, &lock, &error));
    CHECK(tired_transaction_journal_read(directory, &journal, &error));
    CHECK(journal.count == 0 && journal.staging_count == 0);
    for (unsigned i = 1; i <= 3; ++i)
    {
        record.sequence = i;
        record.action = i == 1 ? TIRED_ACTION_PREPARE : TIRED_ACTION_START;
        record.state = i == 1   ? TIRED_ACTION_COMPLETED
                       : i == 2 ? TIRED_ACTION_INTENT
                                : TIRED_ACTION_UNCERTAIN;
        CHECK(tired_transaction_journal_append(directory, lock, &record, &publication, &error));
        CHECK(tired_publication_published(publication) && tired_publication_durable(publication));
        tired_publication_destroy(publication);
        publication = NULL;
    }
    CHECK(tired_transaction_journal_read(directory, &journal, &error));
    CHECK(journal.count == 3 && journal.records[1].state == TIRED_ACTION_INTENT &&
          journal.records[2].state == TIRED_ACTION_UNCERTAIN);
    CHECK(journal.progress.pending && journal.progress.uncertain);
    CHECK(!tired_transaction_journal_append(directory, lock, &record, &publication, &error));
    CHECK(error.status == TIRED_CONFLICT && publication == NULL);
    record.sequence = 4;
    record.action = TIRED_ACTION_COMMIT;
    record.state = TIRED_ACTION_INTENT;
    CHECK(!tired_transaction_journal_append(directory, lock, &record, &publication, &error));
    CHECK(strcmp(error.code, "transaction-transition") == 0 && publication == NULL);
    record.action = TIRED_ACTION_START;
    record.state = TIRED_ACTION_UNCERTAIN;
    record.service_uuid[0] = 'a';
    CHECK(!tired_transaction_journal_append(directory, lock, &record, &publication, &error));
    CHECK(error.status == TIRED_CONFLICT && publication == NULL);
    record.service_uuid[0] = 'f';
    int fd = tired_directory_fd(directory), parent = tired_directory_fd(root);
    CHECK(renameat(fd, "0002.json", parent, "saved") == 0);
    CHECK(!tired_transaction_journal_read(directory, &journal, &error));
    CHECK(journal.count == 3);
    CHECK(renameat(parent, "saved", fd, "0002.json") == 0);
    CHECK(tired_private_file_create(directory, "unexpected", "x", 1, &error));
    CHECK(!tired_transaction_journal_read(directory, &journal, &error));
    CHECK(unlinkat(fd, "unexpected", 0) == 0);
    CHECK(tired_publication_prepare(directory, "0004.json", "incomplete", 10, 0600, &staging,
                                    &error));
    CHECK(tired_transaction_journal_read(directory, &journal, &error));
    CHECK(journal.count == 3 && journal.staging_count == 1);
    CHECK(!tired_transaction_journal_append(directory, lock, &record, &publication, &error));
    CHECK(error.status == TIRED_RECOVERY_REQUIRED && publication == NULL);
    CHECK(tired_publication_discard(staging, &error));
    tired_publication_destroy(staging);
    staging = NULL;
    CHECK(tired_private_file_create(directory, "0004.json", "{", 1, &error));
    CHECK(!tired_transaction_journal_read(directory, &journal, &error));
    CHECK(journal.count == 3);
    CHECK(unlinkat(fd, "0004.json", 0) == 0);
    for (unsigned corrupt = 0; corrupt < 3; ++corrupt)
    {
        record.sequence = corrupt == 0 ? 1 : 4;
        record.service_uuid[0] = corrupt == 1 ? 'a' : 'f';
        record.action = corrupt == 2 ? TIRED_ACTION_COMMIT : TIRED_ACTION_START;
        record.state = corrupt == 2 ? TIRED_ACTION_INTENT : TIRED_ACTION_UNCERTAIN;
        CHECK(tired_transaction_record_encode(&record, &encoded, &error));
        CHECK(tired_private_file_create(directory, "0004.json", encoded.data, encoded.length,
                                        &error));
        CHECK(!tired_transaction_journal_read(directory, &journal, &error));
        CHECK(error.status == TIRED_CONFLICT && journal.count == 3);
        CHECK(unlinkat(fd, "0004.json", 0) == 0);
    }
    record.sequence = 4;
    record.service_uuid[0] = 'f';
    record.action = TIRED_ACTION_START;
    record.state = TIRED_ACTION_UNCERTAIN;
    CHECK(symlinkat("0001.json", fd, "0004.json") == 0);
    CHECK(!tired_transaction_journal_read(directory, &journal, &error));
    CHECK(unlinkat(fd, "0004.json", 0) == 0);
    CHECK(tired_transaction_journal_append(directory, lock, &record, &publication, &error));
    tired_publication_destroy(publication);
    publication = NULL;
    CHECK(tired_transaction_journal_read(directory, &journal, &error));
    CHECK(journal.count == 4 && journal.staging_count == 0);
    result = 0;
cleanup:
    if (publication != NULL)
        (void)tired_publication_discard(publication, &error);
    if (staging != NULL)
        (void)tired_publication_discard(staging, &error);
    tired_publication_destroy(publication);
    tired_publication_destroy(staging);
    tired_transaction_journal_destroy(&journal);
    tired_operation_lock_destroy(lock);
    if (directory != NULL)
    {
        int cleanup_fd = tired_directory_fd(directory);
        const char *names[] = {"0001.json", "0002.json", "0003.json", "0004.json", "unexpected"};
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            (void)unlinkat(cleanup_fd, names[i], 0);
    }
    tired_directory_destroy(directory);
    if (root != NULL)
    {
        int cleanup_fd = tired_directory_fd(root);
        (void)unlinkat(cleanup_fd, "operation.lock", 0);
        (void)unlinkat(cleanup_fd, "saved", 0);
        (void)unlinkat(cleanup_fd, "journal", AT_REMOVEDIR);
    }
    tired_directory_destroy(root);
    if (created != NULL)
        (void)rmdir(created);
    tired_text_destroy(&path);
    tired_text_destroy(&encoded);
    free(cwd);
    return result;
}
