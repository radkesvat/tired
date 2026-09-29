#include "tired/io.h"
#include "tired/manifest_storage.h"
#include "tired/transaction_inventory.h"
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
static bool append(TiredDirectory *journal, TiredOperationLock *lock,
                   TiredTransactionRecord *record, TiredError *error)
{
    TiredPublication *publication = NULL;
    bool ok = tired_transaction_journal_append(journal, lock, record, &publication, error);
    if (publication != NULL)
    {
        TiredError cleanup_error = {0};
        if (!tired_publication_discard(publication, &cleanup_error))
            ok = false;
    }
    tired_publication_destroy(publication);
    return ok;
}
static bool publish_rename(TiredDirectory *directory, TiredOperationLock *lock,
                           const TiredTransactionRecord *prepared, TiredError *error)
{
    TiredFileChange files[3] = {0};
    TiredFileManifest manifest = {.prepared = *prepared, .files = files, .count = 3};
    TiredFileFingerprint snapshot = {
        .exists = true,
        .device = 1,
        .inode = 1,
        .mode = 0644,
        .sha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"};
    for (size_t i = 0; i < 3; ++i)
        memcpy(files[i].target.service_uuid, prepared->service_uuid, 37);
    files[0].target.role = files[1].target.role = TIRED_FILE_TARGET_UNIT;
    files[0].target.unit_name = (TiredText){.data = "old.service", .length = 11};
    files[1].target.unit_name = prepared->unit_name;
    files[0].before = files[1].after = snapshot;
    files[1].after.inode = 2;
    files[2].target.role = TIRED_FILE_TARGET_RECORD;
    snapshot.mode = 0600;
    snapshot.inode = 3;
    files[2].before = files[2].after = snapshot;
    files[2].after.inode = 4;
    for (size_t i = 0; i < 3; ++i)
    {
        if (files[i].before.exists)
        {
            memcpy(files[i].rollback_uuid, prepared->transaction_uuid, 37);
            files[i].rollback_uuid[0] = (char)('a' + i);
        }
        if (files[i].after.exists)
            memcpy(files[i].staging_uuid, prepared->transaction_uuid, 37);
    }
    TiredPublication *publication = NULL;
    bool ok = tired_manifest_publish(directory, lock, &manifest, &publication, error);
    if (publication != NULL)
    {
        TiredError cleanup_error = {0};
        if (!tired_publication_discard(publication, &cleanup_error))
            ok = false;
    }
    tired_publication_destroy(publication);
    return ok;
}
int main(void)
{
    int result = 1;
    char fixture[] = "inventory-test-XXXXXX";
    char *created = NULL, *cwd = getcwd(NULL, 0);
    TiredText path = {0};
    TiredLayout layout = {.user_scope = true};
    TiredDirectory *root = NULL, *transactions = NULL, *transaction = NULL, *journal = NULL;
    TiredDirectory *second = NULL, *second_journal = NULL;
    TiredOperationLock *lock = NULL;
    TiredTransactionInventory inventory = {0};
    TiredError error = {0};
    const TiredTextList *pending = NULL;
    const char *other = "11234567-89ab-4cde-8fab-0123456789ab";
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
    CHECK(tired_operation_lock_acquire(root, &lock, &error));
    CHECK(tired_path_absolute(&path, "transactions", 12, &layout.paths[TIRED_PATH_TRANSACTIONS],
                              &error));
    CHECK(tired_transaction_inventory_load(&layout, &inventory, &error));
    CHECK(inventory.complete && inventory.count == 0);
    struct stat absent;
    CHECK(fstatat(tired_directory_fd(root), "transactions", &absent, AT_SYMLINK_NOFOLLOW) < 0);
    CHECK(tired_directory_child(root, "transactions", true, true, &transactions, &error));
    CHECK(tired_transaction_inventory_load(&layout, &inventory, &error));
    CHECK(inventory.complete && inventory.count == 0);
    CHECK(tired_transaction_inventory_read(transactions, false, &inventory, &error));
    CHECK(inventory.complete && inventory.count == 0);
    CHECK(tired_transaction_inventory_pending(&inventory, &pending, &error) && pending->count == 0);
    CHECK(tired_directory_child(transactions, record.transaction_uuid, true, true, &transaction,
                                &error));
    CHECK(tired_directory_child(transaction, "journal", true, true, &journal, &error));
    CHECK(append(journal, lock, &record, &error));
    CHECK(tired_transaction_inventory_read(transactions, false, &inventory, &error));
    CHECK(inventory.complete && inventory.count == 1 &&
          inventory.entries[0].error.status == TIRED_OK);
    CHECK(tired_transaction_inventory_pending(&inventory, &pending, &error));
    CHECK(pending->count == 1 && strcmp(pending->items[0].data, "relay.service") == 0);
    CHECK(tired_transaction_inventory_read(transactions, true, &inventory, &error));
    CHECK(!inventory.complete && inventory.entries[0].error.status != TIRED_OK);
    CHECK(!tired_transaction_inventory_pending(&inventory, &pending, &error));
    CHECK(error.status == TIRED_RECOVERY_REQUIRED);
    record.sequence = 2;
    record.action = TIRED_ACTION_COMMIT;
    record.state = TIRED_ACTION_INTENT;
    CHECK(append(journal, lock, &record, &error));
    record.sequence = 3;
    record.state = TIRED_ACTION_COMPLETED;
    CHECK(append(journal, lock, &record, &error));
    CHECK(tired_transaction_inventory_read(transactions, false, &inventory, &error));
    CHECK(inventory.complete && inventory.pending_names.count == 0);
    CHECK(inventory.entries[0].progress.mode == TIRED_PROGRESS_COMMITTED);
    CHECK(tired_directory_child(transactions, other, true, true, &second, &error));
    CHECK(tired_transaction_inventory_read(transactions, false, &inventory, &error));
    CHECK(!inventory.complete && inventory.count == 2);
    CHECK(inventory.entries[0].error.status == TIRED_OK &&
          inventory.entries[1].error.status != TIRED_OK);
    CHECK(!tired_transaction_inventory_pending(&inventory, &pending, &error));
    CHECK(tired_directory_child(second, "journal", true, true, &second_journal, &error));
    CHECK(tired_transaction_inventory_read(transactions, false, &inventory, &error));
    CHECK(!inventory.complete);
    record.transaction_uuid[0] = '1';
    record.operation = TIRED_TRANSACTION_RENAME;
    record.sequence = 1;
    record.action = TIRED_ACTION_PREPARE;
    CHECK(append(second_journal, lock, &record, &error));
    CHECK(tired_transaction_inventory_read(transactions, false, &inventory, &error));
    CHECK(!inventory.complete && strcmp(inventory.entries[1].unit_name.data, "relay.service") == 0);
    CHECK(inventory.entries[1].error.status == TIRED_NOT_FOUND);
    CHECK(publish_rename(second, lock, &record, &error));
    CHECK(tired_transaction_inventory_read(transactions, false, &inventory, &error));
    CHECK(inventory.complete && inventory.pending_names.count == 2);
    CHECK(strcmp(inventory.entries[1].previous_unit_name.data, "old.service") == 0);
    CHECK(tired_transaction_inventory_pending(&inventory, &pending, &error));
    CHECK(strcmp(pending->items[0].data, "relay.service") == 0 &&
          strcmp(pending->items[1].data, "old.service") == 0);
    CHECK(unlinkat(tired_directory_fd(second), "files.json", 0) == 0);
    record.approved_sha256[0] = 'a';
    CHECK(publish_rename(second, lock, &record, &error));
    CHECK(tired_transaction_inventory_read(transactions, false, &inventory, &error));
    CHECK(!inventory.complete &&
          strcmp(inventory.entries[1].error.code, "manifest-journal-mismatch") == 0);
    CHECK(tired_transaction_inventory_load(&layout, &inventory, &error));
    CHECK(!inventory.complete && inventory.count == 2);
    CHECK(!tired_transaction_inventory_pending(&inventory, &pending, &error));
    CHECK(tired_text_set(&layout.paths[TIRED_PATH_TRANSACTIONS], "relative", 8, 4096, &error));
    CHECK(!tired_transaction_inventory_load(&layout, &inventory, &error));
    CHECK(inventory.count == 2 && !inventory.complete);
    result = 0;
cleanup:
    tired_transaction_inventory_destroy(&inventory);
    if (journal != NULL)
        for (unsigned i = 1; i <= 3; ++i)
        {
            char name[16];
            (void)snprintf(name, sizeof(name), "%04u.json", i);
            (void)unlinkat(tired_directory_fd(journal), name, 0);
        }
    if (second_journal != NULL)
        (void)unlinkat(tired_directory_fd(second_journal), "0001.json", 0);
    tired_directory_destroy(journal);
    tired_directory_destroy(second_journal);
    if (transaction != NULL)
        (void)unlinkat(tired_directory_fd(transaction), "journal", AT_REMOVEDIR);
    if (second != NULL)
    {
        (void)unlinkat(tired_directory_fd(second), "files.json", 0);
        (void)unlinkat(tired_directory_fd(second), "journal", AT_REMOVEDIR);
    }
    tired_directory_destroy(transaction);
    tired_directory_destroy(second);
    if (transactions != NULL)
    {
        (void)unlinkat(tired_directory_fd(transactions), "01234567-89ab-4cde-8fab-0123456789ab",
                       AT_REMOVEDIR);
        (void)unlinkat(tired_directory_fd(transactions), other, AT_REMOVEDIR);
    }
    tired_directory_destroy(transactions);
    tired_operation_lock_destroy(lock);
    if (root != NULL)
    {
        (void)unlinkat(tired_directory_fd(root), "operation.lock", 0);
        (void)unlinkat(tired_directory_fd(root), "transactions", AT_REMOVEDIR);
    }
    tired_directory_destroy(root);
    if (created != NULL)
        (void)rmdir(created);
    tired_text_destroy(&path);
    tired_layout_destroy(&layout);
    free(cwd);
    return result;
}
