#include "tired/transaction_inventory.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool io_error(TiredError *error)
{
    return tired_error_set(error, TIRED_RUNTIME_FAILED, "transaction-inventory-io",
                           "Cannot inspect transaction inventory.", errno);
}
static bool invalid(TiredError *error, const char *message)
{
    return tired_error_set(error, TIRED_RECOVERY_REQUIRED, "transaction-inventory-incomplete",
                           message, 0);
}
void tired_transaction_inventory_destroy(TiredTransactionInventory *inventory)
{
    if (inventory == NULL)
        return;
    for (size_t i = 0; i < inventory->count; ++i)
    {
        tired_text_destroy(&inventory->entries[i].directory_name);
        tired_text_destroy(&inventory->entries[i].unit_name);
    }
    free(inventory->entries);
    tired_text_list_destroy(&inventory->pending_names);
    *inventory = (TiredTransactionInventory){0};
}
static int compare(const void *a, const void *b)
{
    const TiredText *left = a, *right = b;
    return strcmp(left->data, right->data);
}
static bool inspect(TiredDirectory *root, bool user_scope, TiredTransactionInventoryEntry *entry,
                    size_t *budget, TiredError *error)
{
    TiredDirectory *transaction = NULL, *directory = NULL;
    TiredTransactionJournal journal = {0};
    bool ok = true;
    if (*budget < TIRED_TRANSACTION_SEQUENCE_LIMIT)
    {
        invalid(&entry->error, "Transaction inventory record budget exhausted.");
        return true;
    }
    if (!tired_directory_child(root, entry->directory_name.data, false, true, &transaction,
                               &entry->error) ||
        !tired_directory_child(transaction, "journal", false, true, &directory, &entry->error))
        goto done;
    if (!tired_transaction_journal_read(directory, &journal, &entry->error))
    {
        /* A failed journal read may have consumed its maximum bounded input. */
        *budget -= TIRED_TRANSACTION_SEQUENCE_LIMIT;
        goto done;
    }
    *budget -= journal.count;
    entry->progress = journal.progress;
    if (journal.count == 0)
    {
        invalid(&entry->error, "Transaction journal has no identity record.");
        goto done;
    }
    const TiredTransactionRecord *first = &journal.records[0];
    if (strcmp(first->transaction_uuid, entry->directory_name.data) != 0 ||
        first->user_scope != user_scope)
    {
        invalid(&entry->error,
                "Transaction directory identity or scope does not match its journal.");
        goto done;
    }
    if (!tired_text_set(&entry->unit_name, first->unit_name.data, first->unit_name.length, 208,
                        error))
    {
        ok = false;
        goto done;
    }
    if (journal.staging_count != 0)
        invalid(&entry->error, "Transaction has unexplained journal staging.");
    else if (first->operation == TIRED_TRANSACTION_RENAME &&
             journal.progress.mode != TIRED_PROGRESS_COMMITTED &&
             journal.progress.mode != TIRED_PROGRESS_ROLLED_BACK)
        invalid(&entry->error, "Pending rename requires both names from its transaction manifest.");
done:
    tired_transaction_journal_destroy(&journal);
    tired_directory_destroy(directory);
    tired_directory_destroy(transaction);
    return ok;
}
bool tired_transaction_inventory_read(TiredDirectory *root, bool user_scope,
                                      TiredTransactionInventory *output, TiredError *error)
{
    assert(root != NULL && output != NULL);
    if (!tired_directory_check(root, error))
        return false;
    int fd = tired_directory_fd(root);
    struct stat before, after;
    if (fstat(fd, &before) != 0)
        return io_error(error);
    if (before.st_uid != geteuid() || (before.st_mode & 07777) != 0700)
        return invalid(error, "Transaction inventory requires a private owner directory.");
    int scan_fd = openat(fd, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (scan_fd < 0)
        return io_error(error);
    DIR *scan = fdopendir(scan_fd);
    if (scan == NULL)
    {
        io_error(error);
        (void)close(scan_fd);
        return false;
    }
    TiredTextList names = {0};
    TiredTransactionInventory inventory = {.complete = true};
    bool ok = false;
    for (;;)
    {
        errno = 0;
        struct dirent *entry = readdir(scan);
        if (entry == NULL)
        {
            if (errno != 0)
            {
                io_error(error);
                goto done;
            }
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (!tired_text_list_append(&names, entry->d_name, strlen(entry->d_name), 1024,
                                    TIRED_INPUT_LIMIT, error))
            goto done;
    }
    if (closedir(scan) != 0)
    {
        scan = NULL;
        io_error(error);
        goto done;
    }
    scan = NULL;
    if (names.count != 0)
    {
        qsort(names.items, names.count, sizeof(*names.items), compare);
        inventory.entries = calloc(names.count, sizeof(*inventory.entries));
        if (inventory.entries == NULL)
        {
            tired_error_set(error, TIRED_INTERNAL, "allocation",
                            "Cannot allocate transaction inventory.", errno);
            goto done;
        }
    }
    inventory.count = names.count;
    size_t budget = 16384;
    for (size_t i = 0; i < names.count; ++i)
    {
        TiredTransactionInventoryEntry *entry = &inventory.entries[i];
        entry->directory_name = names.items[i];
        names.items[i] = (TiredText){0};
        if (!inspect(root, user_scope, entry, &budget, error))
            goto done;
        if (entry->error.status != TIRED_OK)
            inventory.complete = false;
        else if (entry->progress.mode != TIRED_PROGRESS_COMMITTED &&
                 entry->progress.mode != TIRED_PROGRESS_ROLLED_BACK)
        {
            if (!tired_text_list_append(&inventory.pending_names, entry->unit_name.data,
                                        entry->unit_name.length, 4096, TIRED_INPUT_LIMIT, error))
                goto done;
        }
    }
    if (!tired_directory_check(root, error))
        goto done;
    if (fstat(fd, &after) != 0)
    {
        io_error(error);
        goto done;
    }
    if (before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
        before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
        before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
        before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
    {
        invalid(error, "Transaction inventory changed during discovery.");
        goto done;
    }
    tired_transaction_inventory_destroy(output);
    *output = inventory;
    inventory = (TiredTransactionInventory){0};
    tired_error_clear(error);
    ok = true;
done:
    if (scan != NULL)
        (void)closedir(scan);
    tired_text_list_destroy(&names);
    tired_transaction_inventory_destroy(&inventory);
    return ok;
}
bool tired_transaction_inventory_pending(const TiredTransactionInventory *inventory,
                                         const TiredTextList **names, TiredError *error)
{
    assert(inventory != NULL && names != NULL);
    if (!inventory->complete)
        return invalid(error,
                       "Incomplete transaction inventory cannot establish name availability.");
    *names = &inventory->pending_names;
    tired_error_clear(error);
    return true;
}
