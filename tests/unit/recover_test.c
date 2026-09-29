#include "../../src/cli/recover_live.h"
#include "tired/io.h"
#include "tired/json.h"
#include "tired/recover_frontend.h"
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
    char fixture[] = "recover-test-XXXXXX";
    char *created = NULL, *cwd = getcwd(NULL, 0);
    char bad_name[] = {'b', 'a', 'd', 27, (char)255, 0};
    const char *uuid = "01234567-89ab-4cde-8fab-0123456789ab";
    TiredDirectory *root = NULL, *state = NULL, *transactions = NULL, *transaction = NULL,
                   *journal = NULL;
    TiredOperationLock *lock = NULL;
    TiredPublication *publication = NULL;
    TiredRequest request = {0};
    TiredText path = {0}, output = {0};
    TiredError error = {0};
    TiredStatus status = TIRED_INTERNAL;
    struct json_object *document = NULL, *entries = NULL, *value = NULL;
    CHECK(cwd != NULL && (created = mkdtemp(fixture)) != NULL);
    TiredText base = {.data = cwd, .length = strlen(cwd)};
    CHECK(tired_path_absolute(&base, created, strlen(created), &path, &error));
    CHECK(tired_directory_open(path.data, getuid(), true, &root, &error));
    CHECK(setenv("XDG_CONFIG_HOME", path.data, 1) == 0 &&
          setenv("XDG_STATE_HOME", path.data, 1) == 0 &&
          setenv("XDG_RUNTIME_DIR", path.data, 1) == 0);
    const char *args[] = {"tired", "recover", "--user", "--json"};
    CHECK(tired_cli_parse(4, args, &request, &error));
    CHECK(tired_recover_command(&request, &output, &status, &error));
    CHECK(status == TIRED_OK);
    CHECK(tired_json_parse(output.data, output.length, TIRED_INPUT_LIMIT, &document, &error));
    CHECK(json_object_object_get_ex(document, "transactions", &entries) &&
          json_object_array_length(entries) == 0);
    CHECK(json_object_object_get_ex(document, "live_reconciliation", &value) &&
          strcmp(json_object_get_string(value), "not_performed") == 0);
    struct stat absent;
    CHECK(fstatat(tired_directory_fd(root), "tired", &absent, AT_SYMLINK_NOFOLLOW) < 0);
    CHECK(tired_directory_child(root, "tired", true, true, &state, &error));
    CHECK(tired_directory_child(state, "transactions", true, true, &transactions, &error));
    CHECK(tired_directory_child(transactions, uuid, true, true, &transaction, &error));
    CHECK(tired_directory_child(transaction, "journal", true, true, &journal, &error));
    CHECK(tired_operation_lock_acquire(root, &lock, &error));
    TiredTransactionRecord record = {
        .transaction_uuid = "01234567-89ab-4cde-8fab-0123456789ab",
        .service_uuid = "fedcba98-7654-4321-abcd-fedcba987654",
        .approved_sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        .unit_name = {.data = "relay.service", .length = 13},
        .user_scope = true,
        .operation = TIRED_TRANSACTION_CREATE,
        .action = TIRED_ACTION_PREPARE,
        .state = TIRED_ACTION_COMPLETED,
        .sequence = 1};
    CHECK(tired_transaction_journal_append(journal, lock, &record, &publication, &error));
    CHECK(tired_recover_command(&request, &output, &status, &error));
    CHECK(status == TIRED_RECOVERY_REQUIRED);
    CHECK(tired_json_parse(output.data, output.length, TIRED_INPUT_LIMIT, &document, &error));
    CHECK(json_object_object_get_ex(document, "transactions", &entries) &&
          json_object_array_length(entries) == 1);
    struct json_object *row = json_object_array_get_idx(entries, 0);
    CHECK(json_object_object_get_ex(row, "journal_state", &value) &&
          strcmp(json_object_get_string(value), "forward") == 0);
    CHECK(json_object_object_get_ex(document, "exit_code", &value) &&
          json_object_get_int(value) == 8);
    CHECK(json_object_object_get_ex(document, "live_observations", &value) &&
          strcmp(json_object_get_string(value), "unavailable") == 0);
    struct json_object *live_row = NULL;
    CHECK(json_object_object_get_ex(row, "live", &live_row));
    CHECK(json_object_object_get_ex(live_row, "status", &value) &&
          strcmp(json_object_get_string(value), "unknown") == 0);
    CHECK(!json_object_object_get_ex(live_row, "object_found", &value));
    CHECK(json_object_object_get_ex(document, "resolution_actions_supported", &value) &&
          !json_object_get_boolean(value));
    CHECK(mkdirat(tired_directory_fd(transactions), bad_name, 0700) == 0);
    CHECK(tired_recover_command(&request, &output, &status, &error));
    CHECK(tired_json_parse(output.data, output.length, TIRED_INPUT_LIMIT, &document, &error));
    CHECK(json_object_object_get_ex(document, "inventory_complete", &value) &&
          !json_object_get_boolean(value));
    CHECK(json_object_object_get_ex(document, "transactions", &entries) &&
          json_object_array_length(entries) == 2);
    row = json_object_array_get_idx(entries, 1);
    CHECK(json_object_object_get_ex(row, "directory_name_display", &value) &&
          strcmp(json_object_get_string(value), "bad\\x1b\\xff") == 0);
    request.json = false;
    CHECK(tired_recover_command(&request, &output, &status, &error));
    CHECK(strstr(output.data, "bad\\x1b\\xff") != NULL &&
          strstr(output.data, "relay.service") != NULL);
    CHECK(memchr(output.data, 27, output.length) == NULL &&
          memchr(output.data, 255, output.length) == NULL);
    CHECK(setenv("XDG_STATE_HOME", "relative", 1) == 0);
    CHECK(!tired_recover_command(&request, &output, &status, &error));
    CHECK(status == TIRED_RECOVERY_REQUIRED && strstr(output.data, "bad\\x1b\\xff") != NULL);
    TiredUnitObservation observation = {0};
    observation.fields[TIRED_OBS_MAIN_PID].known = true;
    observation.fields[TIRED_OBS_MAIN_PID].value.unsigned_value = 0;
    TiredUnitBatchItem item = {
        .attempted = true,
        .completed_realtime_usec = 123,
        .query = {.done = true, .object_found = true, .observation = &observation}};
    json_object_put(document);
    document = tired_recover_live_json(&item);
    CHECK(document != NULL && json_object_object_get_ex(document, "properties", &entries));
    CHECK(json_object_object_get_ex(entries, "MainPID", &value) && json_object_get_int(value) == 0);
    CHECK(!json_object_object_get_ex(entries, "ActiveState", &value));
    item.query.error =
        (TiredError){.status = TIRED_AUTHORIZATION, .code = "fixture-denied", .message = "Denied."};
    json_object_put(document);
    document = tired_recover_live_json(&item);
    CHECK(document != NULL && !json_object_object_get_ex(document, "properties", &entries));
    CHECK(!json_object_object_get_ex(document, "object_found", &value));
    result = 0;
cleanup:
    json_object_put(document);
    tired_publication_destroy(publication);
    tired_operation_lock_destroy(lock);
    if (journal != NULL)
        (void)unlinkat(tired_directory_fd(journal), "0001.json", 0);
    tired_directory_destroy(journal);
    if (transaction != NULL)
        (void)unlinkat(tired_directory_fd(transaction), "journal", AT_REMOVEDIR);
    tired_directory_destroy(transaction);
    if (transactions != NULL)
    {
        (void)unlinkat(tired_directory_fd(transactions), uuid, AT_REMOVEDIR);
        (void)unlinkat(tired_directory_fd(transactions), bad_name, AT_REMOVEDIR);
    }
    tired_directory_destroy(transactions);
    if (state != NULL)
        (void)unlinkat(tired_directory_fd(state), "transactions", AT_REMOVEDIR);
    tired_directory_destroy(state);
    if (root != NULL)
    {
        (void)unlinkat(tired_directory_fd(root), "tired", AT_REMOVEDIR);
        (void)unlinkat(tired_directory_fd(root), "operation.lock", 0);
    }
    tired_directory_destroy(root);
    if (created != NULL)
        (void)rmdir(created);
    tired_request_destroy(&request);
    tired_text_destroy(&path);
    tired_text_destroy(&output);
    free(cwd);
    return result;
}
