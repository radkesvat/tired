#include "../../src/cli/show_effective.h"
#include "tired/effective_files.h"
#include "tired/encode.h"
#include "tired/file_target.h"
#include "tired/io.h"
#include "tired/list_frontend.h"
#include "tired/private_file.h"
#include "tired/render.h"
#include "tired/service_files.h"
#include "tired/service_inventory.h"
#include "tired/service_record.h"
#include "tired/service_record_storage.h"
#include "tired/show_frontend.h"
#include "tired/status_frontend.h"
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
            fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #expression,                   \
                    error.code == NULL ? "none" : error.code);                                     \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
static bool set(TiredText *output, const char *text, TiredError *error)
{
    return tired_text_set(output, text, strlen(text), TIRED_INPUT_LIMIT, error);
}
static int file_checks(TiredLayout *layout, TiredServiceRecord *record, TiredDirectory *directory)
{
    int result = 1;
    TiredError error = {0};
    TiredText unit = {0};
    char *large_unit = NULL;
    TiredServiceFiles files = {0};
    TiredFileFingerprint fingerprint = {0};
    TiredDirectory *service = NULL, *revisions = NULL, *revision = NULL;
    TiredResolvedFile resolved = {0};
    int fd = tired_directory_fd(directory);
    CHECK(tired_service_files_inspect(layout, record, &files, &error));
    CHECK(files.unit.state == TIRED_SERVICE_FILE_MISSING &&
          files.environment.state == TIRED_SERVICE_FILE_NOT_REQUIRED);
    CHECK(tired_render_unit(&record->spec, record->metadata.service_uuid, NULL,
                            &record->credentials, &unit, &error));
    CHECK(tired_private_file_create(directory, "relay.service", unit.data, unit.length, &error));
    CHECK(fchmodat(fd, "relay.service", 0644, 0) == 0);
    CHECK(tired_file_fingerprint(directory, "relay.service", TIRED_INPUT_LIMIT, &fingerprint,
                                 &error));
    memcpy(record->metadata.unit_sha256, fingerprint.sha256, 65);
    CHECK(tired_service_files_inspect(layout, record, &files, &error));
    CHECK(files.unit.state == TIRED_SERVICE_FILE_MATCH && files.unit.marker_matches);
    size_t remaining = unit.length;
    CHECK(tired_service_files_inspect_budget(layout, record, &remaining, &files, &error));
    CHECK(remaining == 0 && files.unit.state == TIRED_SERVICE_FILE_MATCH);
    CHECK(tired_service_files_inspect_budget(layout, record, &remaining, &files, &error));
    CHECK(files.unit.state == TIRED_SERVICE_FILE_UNKNOWN &&
          files.unit.error.status == TIRED_RECOVERY_REQUIRED);
    CHECK(fchmodat(fd, "relay.service", 0600, 0) == 0);
    CHECK(tired_service_files_inspect(layout, record, &files, &error));
    CHECK(files.unit.state == TIRED_SERVICE_FILE_DRIFTED && !files.unit.mode_matches &&
          files.unit.digest_matches && files.unit.owner_matches);
    CHECK(fchmodat(fd, "relay.service", 0644, 0) == 0);
    record->metadata.unit_sha256[0] = fingerprint.sha256[0] == '0' ? '1' : '0';
    CHECK(tired_service_files_inspect(layout, record, &files, &error));
    CHECK(files.unit.state == TIRED_SERVICE_FILE_DRIFTED && !files.unit.digest_matches);
    memcpy(record->metadata.unit_sha256, fingerprint.sha256, 65);
    record->metadata.service_uuid[0] = '1';
    CHECK(tired_service_files_inspect(layout, record, &files, &error));
    CHECK(files.unit.state == TIRED_SERVICE_FILE_DRIFTED && !files.unit.marker_matches &&
          files.unit.digest_matches);
    record->metadata.service_uuid[0] = '0';
    CHECK(linkat(fd, "relay.service", fd, "extra-link", 0) == 0);
    CHECK(tired_service_files_inspect(layout, record, &files, &error));
    CHECK(files.unit.state == TIRED_SERVICE_FILE_UNKNOWN && files.unit.error.status != TIRED_OK);
    CHECK(unlinkat(fd, "extra-link", 0) == 0 && unlinkat(fd, "relay.service", 0) == 0);
    CHECK(symlinkat("missing", fd, "relay.service") == 0);
    CHECK(tired_service_files_inspect(layout, record, &files, &error));
    CHECK(files.unit.state == TIRED_SERVICE_FILE_UNKNOWN);
    CHECK(unlinkat(fd, "relay.service", 0) == 0);
    CHECK(tired_private_file_create(directory, "relay.service", unit.data, unit.length, &error));
    CHECK(fchmodat(fd, "relay.service", 0644, 0) == 0);
    CHECK(set(&layout->paths[TIRED_PATH_ENVIRONMENT_SERVICES], layout->paths[TIRED_PATH_UNITS].data,
              &error));
    record->has_environment = true;
    memcpy(record->environment_revision, "31234567-89ab-4cde-8fab-0123456789ab", 37);
    TiredFileTarget target = {.role = TIRED_FILE_TARGET_ENVIRONMENT};
    memcpy(target.service_uuid, record->metadata.service_uuid, 37);
    memcpy(target.revision_uuid, record->environment_revision, 37);
    CHECK(tired_file_target_resolve(layout, &target, &resolved, &error));
    CHECK(tired_path_absolute(&resolved.directory, "environment", 11, &record->environment_path,
                              &error));
    CHECK(tired_service_files_inspect(layout, record, &files, &error));
    CHECK(files.unit.state == TIRED_SERVICE_FILE_MATCH &&
          files.environment.state == TIRED_SERVICE_FILE_MISSING);
    CHECK(tired_directory_child(directory, record->metadata.service_uuid, true, true, &service,
                                &error));
    CHECK(tired_directory_child(service, "revisions", true, true, &revisions, &error));
    CHECK(tired_directory_child(revisions, record->environment_revision, true, true, &revision,
                                &error));
    CHECK(tired_private_file_create(revision, "environment", "TOKEN=secret\n", 13, &error));
    CHECK(tired_file_fingerprint(revision, "environment", TIRED_INPUT_LIMIT, &fingerprint, &error));
    memcpy(record->environment_sha256, fingerprint.sha256, 65);
    CHECK(tired_service_files_inspect(layout, record, &files, &error));
    CHECK(files.environment.state == TIRED_SERVICE_FILE_MATCH);
    record->environment_sha256[0] = fingerprint.sha256[0] == '0' ? '1' : '0';
    CHECK(tired_service_files_inspect(layout, record, &files, &error));
    CHECK(files.environment.state == TIRED_SERVICE_FILE_DRIFTED &&
          !files.environment.digest_matches && files.unit.state == TIRED_SERVICE_FILE_MATCH);
    memcpy(record->environment_sha256, fingerprint.sha256, 65);
    CHECK(fchmodat(tired_directory_fd(revision), "environment", 0644, 0) == 0);
    CHECK(tired_service_files_inspect(layout, record, &files, &error));
    CHECK(files.environment.state == TIRED_SERVICE_FILE_DRIFTED &&
          !files.environment.mode_matches && files.environment.digest_matches);
    CHECK(fchmod(tired_directory_fd(revision), 0755) == 0);
    CHECK(tired_service_files_inspect(layout, record, &files, &error));
    CHECK(files.unit.state == TIRED_SERVICE_FILE_MATCH &&
          files.environment.state == TIRED_SERVICE_FILE_UNKNOWN);
    size_t large_length = TIRED_INPUT_LIMIT + unit.length;
    large_unit = malloc(large_length);
    CHECK(large_unit != NULL);
    memcpy(large_unit, unit.data, unit.length);
    memset(large_unit + unit.length, '\n', TIRED_INPUT_LIMIT);
    CHECK(unlinkat(fd, "relay.service", 0) == 0);
    CHECK(tired_private_file_create(directory, "relay.service", large_unit, large_length, &error));
    CHECK(fchmodat(fd, "relay.service", 0644, 0) == 0);
    CHECK(
        tired_file_fingerprint(directory, "relay.service", TIRED_UNIT_LIMIT, &fingerprint, &error));
    memcpy(record->metadata.unit_sha256, fingerprint.sha256, 65);
    CHECK(tired_service_files_inspect(layout, record, &files, &error));
    CHECK(files.unit.state == TIRED_SERVICE_FILE_MATCH);
    TiredServiceFiles saved = files;
    record->metadata.user_scope = false;
    CHECK(!tired_service_files_inspect(layout, record, &files, &error));
    CHECK(memcmp(&files, &saved, sizeof(files)) == 0);
    record->metadata.user_scope = true;
    uid_t owner = record->metadata.owner_uid;
    record->metadata.owner_uid = owner == 0 ? 1 : 0;
    CHECK(!tired_service_files_inspect(layout, record, &files, &error));
    CHECK(error.status == TIRED_CONFLICT && memcmp(&files, &saved, sizeof(files)) == 0);
    record->metadata.owner_uid = owner;
    result = 0;
cleanup:
    (void)unlinkat(fd, "relay.service", 0);
    (void)unlinkat(fd, "extra-link", 0);
    if (revision != NULL)
        (void)unlinkat(tired_directory_fd(revision), "environment", 0);
    if (revisions != NULL)
        (void)unlinkat(tired_directory_fd(revisions), record->environment_revision, AT_REMOVEDIR);
    if (service != NULL)
        (void)unlinkat(tired_directory_fd(service), "revisions", AT_REMOVEDIR);
    (void)unlinkat(fd, record->metadata.service_uuid, AT_REMOVEDIR);
    tired_directory_destroy(revision);
    tired_directory_destroy(revisions);
    tired_directory_destroy(service);
    tired_resolved_file_destroy(&resolved);
    tired_text_destroy(&unit);
    free(large_unit);
    return result;
}
static int effective_checks(TiredDirectory *units, const TiredText *path,
                            TiredServiceRecord *record)
{
    int result = 1;
    TiredError error = {0};
    TiredText drop_path = {0}, missing_path = {0};
    TiredEffectiveFiles effective = {0};
    struct json_object *report = NULL, *rows = NULL, *value = NULL;
    TiredBuffer text;
    tired_buffer_init(&text, TIRED_UNIT_LIMIT);
    CHECK(tired_path_absolute(path, "systemd/user/override.conf", 26, &drop_path, &error));
    CHECK(tired_path_absolute(path, "systemd/user/missing.conf", 25, &missing_path, &error));
    const char dropin[] = "[Service]\nEnvironment=API_TOKEN=drop-private\n";
    CHECK(tired_private_file_create(units, "override.conf", dropin, sizeof(dropin) - 1, &error));
    TiredText dropins[] = {drop_path, missing_path};
    TiredUnitObservation observed = {0};
    observed.fields[TIRED_OBS_FRAGMENT_PATH] =
        (TiredObservedValue){.known = true, .value.text = record->unit_path};
    observed.fields[TIRED_OBS_DROP_IN_PATHS] =
        (TiredObservedValue){.known = true, .value.list = {.items = dropins, .count = 1}};
    TiredUnitQueryResult query = {.done = true,
                                  .object_found = true,
                                  .unit_name = record->metadata.unit_name.data,
                                  .observation = &observed};
    CHECK(tired_effective_files_collect(record, &query, false, &effective, &error));
    CHECK(effective.complete && effective.count == 2 &&
          strstr(effective.files[0].display.data, "secret") == NULL &&
          strstr(effective.files[1].display.data, "drop-private") == NULL);
    TiredUnitBatchItem live = {.attempted = true, .completed_realtime_usec = 123, .query = query};
    report = tired_show_effective_json(&effective, &live, true);
    CHECK(report != NULL && json_object_object_get_ex(report, "files", &rows) &&
          json_object_array_length(rows) == 2);
    CHECK(json_object_object_get_ex(json_object_array_get_idx(rows, 1), "text", &value) &&
          strstr(json_object_get_string(value), "drop-private") == NULL);
    CHECK(tired_show_effective_text(&effective, true, &text, &error));
    CHECK(strstr(text.data, "# Fragment:") != NULL && strstr(text.data, "# Drop-in:") != NULL &&
          strstr(text.data, "secret") == NULL && memchr(text.data, 27, text.length) == NULL);
    json_object_put(report);
    report = tired_show_effective_json(&effective, &live, false);
    CHECK(report != NULL && json_object_object_get_ex(report, "files", &rows) &&
          !json_object_object_get_ex(json_object_array_get_idx(rows, 0), "text", &value));
    CHECK(tired_effective_files_collect(record, &query, true, &effective, &error));
    CHECK(effective.complete && strcmp(effective.files[1].display.data, dropin) == 0);
    observed.fields[TIRED_OBS_DROP_IN_PATHS].value.list.count = 2;
    CHECK(tired_effective_files_collect(record, &query, false, &effective, &error));
    CHECK(!effective.complete && effective.count == 3 &&
          effective.files[1].error.status == TIRED_OK &&
          effective.files[2].error.status == TIRED_NOT_FOUND);
    observed.fields[TIRED_OBS_DROP_IN_PATHS].known = false;
    CHECK(tired_effective_files_collect(record, &query, false, &effective, &error));
    CHECK(!effective.complete && effective.count == 1 &&
          effective.error.status == TIRED_RECOVERY_REQUIRED);
    query.unit_name = "other.service";
    CHECK(!tired_effective_files_collect(record, &query, false, &effective, &error));
    CHECK(effective.count == 1 && error.status == TIRED_CONFLICT);
    query.error =
        (TiredError){.status = TIRED_AUTHORIZATION, .code = "fixture-denied", .message = "Denied."};
    CHECK(tired_effective_files_collect(record, &query, false, &effective, &error));
    CHECK(!effective.complete && effective.count == 0 &&
          effective.error.status == TIRED_AUTHORIZATION);
    result = 0;
cleanup:
    json_object_put(report);
    tired_buffer_destroy(&text);
    (void)unlinkat(tired_directory_fd(units), "override.conf", 0);
    tired_effective_files_destroy(&effective);
    tired_text_destroy(&drop_path);
    tired_text_destroy(&missing_path);
    return result;
}
static int show_checks(TiredDirectory *root, const TiredText *path, TiredServiceRecord *record)
{
    int result = 1;
    TiredError error = {0};
    TiredRequest request = {0};
    TiredText output = {0}, rendered = {0}, exported = {0};
    TiredDirectory *systemd = NULL, *units = NULL;
    TiredBuffer original;
    tired_buffer_init(&original, TIRED_UNIT_LIMIT);
    TiredStatus status = TIRED_INTERNAL;
    const char *args[] = {"tired", "show", "relay", "--user", "--json"};
    CHECK(tired_cli_parse((int)(sizeof(args) / sizeof(args[0])), args, &request, &error));
    CHECK(tired_show_command(&request, &output, &status, &error));
    CHECK(status == TIRED_OK && strstr(output.data, "\"status\":\"missing\"") != NULL &&
          strstr(output.data, "\"command\":\"show\"") != NULL &&
          strstr(output.data, "secret") == NULL);
    CHECK(tired_directory_child(root, "systemd", true, true, &systemd, &error));
    CHECK(tired_directory_child(systemd, "user", true, true, &units, &error));
    CHECK(tired_render_unit(&record->spec, record->metadata.service_uuid, NULL,
                            &record->credentials, &rendered, &error));
    const char extra[] = "\n# installed comment \x1b[31m\n";
    CHECK(tired_buffer_append(&original, rendered.data, rendered.length, &error) &&
          tired_buffer_append(&original, extra, sizeof(extra) - 1, &error));
    CHECK(
        tired_private_file_create(units, "relay.service", original.data, original.length, &error));
    CHECK(fchmodat(tired_directory_fd(units), "relay.service", 0644, 0) == 0);
    CHECK(effective_checks(units, path, record) == 0);
    CHECK(tired_show_command(&request, &output, &status, &error));
    CHECK(status == TIRED_OK && strstr(output.data, "\"digest_matches\":false") != NULL &&
          strstr(output.data, "\"marker_matches\":true") != NULL &&
          strstr(output.data, "installed comment") != NULL &&
          strstr(output.data, "secret") == NULL && memchr(output.data, 27, output.length) == NULL);
    request.effective = true;
    CHECK(tired_show_command(&request, &output, &status, &error));
    CHECK(status == TIRED_RECOVERY_REQUIRED && strstr(output.data, "\"effective\":") != NULL &&
          strstr(output.data, "\"complete\":false") != NULL &&
          strstr(output.data, "secret") == NULL);
    request.unit = true;
    request.json = false;
    CHECK(tired_show_command(&request, &output, &status, &error));
    CHECK(status == TIRED_RECOVERY_REQUIRED &&
          strstr(output.data, "Effective file inspection") != NULL);
    request.effective = false;
    CHECK(tired_show_command(&request, &output, &status, &error));
    CHECK(strstr(output.data, "Terminal-escaped, non-installable") != NULL &&
          strstr(output.data, "Redacted, non-installable") != NULL &&
          strstr(output.data, "installed comment") != NULL &&
          strstr(output.data, "secret") == NULL && memchr(output.data, 27, output.length) == NULL);
    char *previous = output.data;
    request.include_sensitive = true;
    CHECK(!tired_show_command(&request, &output, &status, &error));
    CHECK(output.data == previous && error.status == TIRED_INVALID);
    CHECK(tired_path_absolute(path, "export.unit", 11, &request.output, &error));
    CHECK(!tired_show_command(&request, &output, &status, &error));
    CHECK(tired_text_list_append(&request.allowed_risks, "sensitive-export", 16, 16, 1024, &error));
    CHECK(tired_show_command(&request, &output, &status, &error));
    CHECK(strstr(output.data, "secret") == NULL && status == TIRED_OK);
    struct stat info;
    CHECK(fstatat(tired_directory_fd(root), "export.unit", &info, AT_SYMLINK_NOFOLLOW) == 0 &&
          (info.st_mode & 0777) == 0600);
    CHECK(tired_read_file(request.output.data, TIRED_UNIT_LIMIT, &exported, &error));
    CHECK(exported.length == original.length &&
          memcmp(exported.data, original.data, exported.length) == 0);
    CHECK(!tired_show_command(&request, &output, &status, &error));
    CHECK(error.status == TIRED_CONFLICT);
    request.include_sensitive = false;
    tired_text_destroy(&request.output);
    CHECK(unlinkat(tired_directory_fd(units), "relay.service", 0) == 0);
    CHECK(symlinkat("missing", tired_directory_fd(units), "relay.service") == 0);
    CHECK(!tired_show_command(&request, &output, &status, &error));
    request.unit = false;
    request.json = true;
    CHECK(tired_show_command(&request, &output, &status, &error));
    CHECK(status == TIRED_RECOVERY_REQUIRED &&
          strstr(output.data, "\"status\":\"unknown\"") != NULL);
    result = 0;
cleanup:
    (void)unlinkat(tired_directory_fd(root), "export.unit", 0);
    if (units != NULL)
        (void)unlinkat(tired_directory_fd(units), "relay.service", 0);
    if (systemd != NULL)
        (void)unlinkat(tired_directory_fd(systemd), "user", AT_REMOVEDIR);
    (void)unlinkat(tired_directory_fd(root), "systemd", AT_REMOVEDIR);
    tired_directory_destroy(units);
    tired_directory_destroy(systemd);
    tired_request_destroy(&request);
    tired_text_destroy(&output);
    tired_text_destroy(&rendered);
    tired_text_destroy(&exported);
    tired_buffer_destroy(&original);
    return result;
}
static int status_checks(TiredDirectory *root, const TiredText *path, TiredServiceRecord *record)
{
    int result = 1;
    TiredError error = {0};
    TiredDirectory *state = NULL, *records = NULL;
    TiredText encoded = {0}, output = {0}, saved_path = record->unit_path;
    record->unit_path = (TiredText){0};
    TiredRequest request = {0};
    TiredStatus status = TIRED_INTERNAL;
    char filename[42];
    (void)snprintf(filename, sizeof(filename), "%s.json", record->metadata.service_uuid);
    CHECK(tired_path_absolute(path, "systemd/user/relay.service", 26, &record->unit_path, &error));
    CHECK(tired_directory_child(root, "tired", true, true, &state, &error));
    CHECK(tired_directory_child(state, "services", true, true, &records, &error));
    CHECK(tired_service_record_encode(record, &encoded, &error));
    CHECK(tired_private_file_create(records, filename, encoded.data, encoded.length, &error));
    CHECK(setenv("XDG_STATE_HOME", path->data, 1) == 0 &&
          setenv("XDG_CONFIG_HOME", path->data, 1) == 0 &&
          setenv("XDG_RUNTIME_DIR", path->data, 1) == 0);
    const char *args[] = {"tired", "status", "relay", "--user", "--json"};
    CHECK(tired_cli_parse((int)(sizeof(args) / sizeof(args[0])), args, &request, &error));
    CHECK(tired_status_command(&request, &output, &status, &error));
    CHECK(strstr(output.data, "\"record\":\"present\"") != NULL &&
          strstr(output.data, record->metadata.service_uuid) != NULL &&
          strstr(output.data, "\"state\":\"missing\"") != NULL &&
          strstr(output.data, "secret") == NULL);
    CHECK(status != TIRED_OK); /* Isolated runtime directory contains no bus. */
    const char *list_args[] = {"tired", "list", "--user", "--json"};
    CHECK(tired_cli_parse((int)(sizeof(list_args) / sizeof(list_args[0])), list_args, &request,
                          &error));
    CHECK(tired_list_command(&request, &output, &status, &error));
    CHECK(strstr(output.data, "\"command\":\"list\"") != NULL &&
          strstr(output.data, "\"record\":\"present\"") != NULL &&
          strstr(output.data, "\"profile\":\"generic\"") != NULL &&
          strstr(output.data, "secret") == NULL && status != TIRED_OK);
    CHECK(show_checks(root, path, record) == 0);
    CHECK(set(&request.search, "ReLaY", &error));
    CHECK(set(&request.active_filter, "active", &error));
    CHECK(tired_list_command(&request, &output, &status, &error));
    CHECK(strstr(output.data, "\"filter_unknown\":1") != NULL &&
          strstr(output.data, "\"filter_match\":\"unknown\"") != NULL);
    CHECK(set(&request.profile, "different-profile", &error));
    CHECK(tired_list_command(&request, &output, &status, &error));
    CHECK(strstr(output.data, "\"filtered_out\":1") != NULL &&
          strstr(output.data, "relay.service") == NULL && status != TIRED_OK);
    tired_text_destroy(&request.search);
    tired_text_destroy(&request.active_filter);
    tired_text_destroy(&request.profile);
    int bad = openat(tired_directory_fd(records), "bad\n\xff",
                     O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    CHECK(bad >= 0 && close(bad) == 0);
    CHECK(tired_list_command(&request, &output, &status, &error));
    CHECK(status == TIRED_RECOVERY_REQUIRED &&
          strstr(output.data, "\"inventory_complete\":false") != NULL &&
          strstr(output.data, "\"record\":\"present\"") != NULL);
    request.json = false;
    CHECK(tired_list_command(&request, &output, &status, &error));
    CHECK(strstr(output.data, "bad\\x0a\\xff") != NULL &&
          strstr(output.data, "relay.service") != NULL &&
          memchr(output.data, 255, output.length) == NULL);
    result = 0;
cleanup:
    if (records != NULL)
    {
        (void)unlinkat(tired_directory_fd(records), "bad\n\xff", 0);
        (void)unlinkat(tired_directory_fd(records), filename, 0);
    }
    if (state != NULL)
        (void)unlinkat(tired_directory_fd(state), "services", AT_REMOVEDIR);
    (void)unlinkat(tired_directory_fd(root), "tired", AT_REMOVEDIR);
    tired_directory_destroy(records);
    tired_directory_destroy(state);
    tired_request_destroy(&request);
    tired_text_destroy(&encoded);
    tired_text_destroy(&output);
    tired_text_destroy(&record->unit_path);
    record->unit_path = saved_path;
    return result;
}
int main(int argc, char **argv)
{
    int result = 1;
    char fixture[] = "record-storage-XXXXXX", name[42], other[42];
    char *created = NULL, *cwd = NULL;
    TiredDirectory *directory = NULL;
    TiredServiceInventory inventory = {0};
    const TiredServiceMetadata *found = NULL;
    TiredText directory_path = {0};
    TiredServiceRecord
        source =
            {.metadata = {.service_uuid = "01234567-89ab-4cde-8fab-0123456789ab",
                          .revision_uuid = "11234567-89ab-4cde-8fab-0123456789ab",
                          .transaction_uuid = "21234567-89ab-4cde-8fab-0123456789ab",
                          .unit_sha256 =
                              "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                          .user_scope = true,
                          .owner_uid = 1000,
                          .invoking_uid = 1000,
                          .service_uid = 1000,
                          .invoking_gid = 1000,
                          .service_gid = 1000,
                          .created_usec = 1,
                          .updated_usec = 2},
             .review = {.approved_sha256 =
                            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                        .argument_count = 2},
             .has_environment = true,
             .environment_revision = "31234567-89ab-4cde-8fab-0123456789ab",
             .environment_sha256 =
                 "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"},
        parsed = {0};
    TiredLayout layout = {0};
    TiredText encoded = {0}, again = {0};
    TiredError error = {0};
    struct json_object *document = NULL, *review = NULL;
    CHECK(set(&source.metadata.unit_name, "relay.service", &error));
    CHECK(set(&source.metadata.writer_version, "0.1.0", &error));
    CHECK(tired_spec_defaults(&source.spec, &error));
    CHECK(tired_spec_set(&source.spec, TIRED_FIELD_NAME, "relay", 5, TIRED_ORIGIN_USER, true,
                         &error));
    CHECK(tired_spec_set(&source.spec, TIRED_FIELD_SCOPE, "user", 4, TIRED_ORIGIN_USER, true,
                         &error));
    CHECK(tired_spec_resolve_scope(&source.spec, &error));
    CHECK(tired_spec_set(&source.spec, TIRED_FIELD_EXECUTABLE, "/missing/app", 12,
                         TIRED_ORIGIN_CAPTURE, true, &error));
    CHECK(tired_spec_set(&source.spec, TIRED_FIELD_WORKING_DIRECTORY, "/missing", 8,
                         TIRED_ORIGIN_CAPTURE, true, &error));
    CHECK(tired_spec_append(&source.spec, TIRED_FIELD_ARGV, "/missing/app", 12,
                            TIRED_ORIGIN_CAPTURE, &error));
    CHECK(tired_spec_append(&source.spec, TIRED_FIELD_ARGV, "secret", 6, TIRED_ORIGIN_CAPTURE,
                            &error));
    source.review.sensitive_arguments[1] = true;
    CHECK(set(&source.executable.lexical_path, "/missing/app", &error));
    CHECK(set(&source.executable.resolved_path, "/missing/resolved", &error));
    source.executable.device = 1;
    source.executable.inode = 2;
    CHECK(tired_environment_set(&source.environment, "TOKEN=hidden", 12, TIRED_ENV_EXPLICIT, true,
                                &error));
    CHECK(tired_credentials_add(&source.credentials, "key=/missing/key", 16, &error));
    CHECK(set(&source.unit_path, "/fixture/config/systemd/user/relay.service", &error));
    CHECK(set(&source.environment_path,
              "/fixture/config/tired/services/01234567-89ab-4cde-8fab-0123456789ab/revisions/"
              "31234567-89ab-4cde-8fab-0123456789ab/environment",
              &error));
    CHECK(tired_text_list_append(&source.external_config_paths, "/missing/app.toml", 17, 256,
                                 TIRED_INPUT_LIMIT, &error));
    CHECK(tired_layout_resolve(true, "/fixture", "/fixture/config", "/fixture/state",
                               "/fixture/run", &layout, &error));
    CHECK(tired_service_record_check_layout(&source, &layout, &error));
    CHECK(tired_service_record_encode(&source, &encoded, &error));
    CHECK(tired_service_record_parse(encoded.data, encoded.length, &parsed, &error));
    CHECK(parsed.metadata.owner_uid == 1000 && parsed.review.sensitive_arguments[1] &&
          parsed.has_environment && !parsed.has_profile);
    CHECK(parsed.environment.count == 1 && parsed.credentials.count == 1 &&
          parsed.external_config_paths.count == 1);
    CHECK(tired_service_record_check_layout(&parsed, &layout, &error));
    CHECK(tired_service_record_encode(&parsed, &again, &error));
    CHECK(strcmp(encoded.data, again.data) == 0);
    CHECK(tired_json_parse(encoded.data, encoded.length, TIRED_SERVICE_RECORD_LIMIT, &document,
                           &error));
    CHECK(json_object_object_get_ex(document, "review", &review));
    CHECK(json_object_object_add(review, "argument_count", json_object_new_int(3)) == 0);
    const char *bad = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_service_record_parse(bad, strlen(bad), &parsed, &error));
    CHECK(parsed.review.argument_count == 2);
    CHECK(json_object_object_add(review, "argument_count", json_object_new_int(2)) == 0);
    CHECK(json_object_object_add(document, "owned_environment", NULL) == 0);
    bad = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_service_record_parse(bad, strlen(bad), &parsed, &error));
    CHECK(set(&source.unit_path, "/elsewhere/relay.service", &error));
    CHECK(!tired_service_record_check_layout(&source, &layout, &error));
    CHECK(set(&source.unit_path, parsed.unit_path.data, &error));
    CHECK(argc == 2 && tired_read_file(argv[1], TIRED_PROFILE_LIMIT, &again, &error));
    CHECK(tired_profile_parse(again.data, again.length, &source.profile.profile, &error));
    CHECK(source.profile.profile.count == 0);
    CHECK(set(&source.profile.source_path, argv[1], &error));
    memcpy(source.profile.source_sha256, source.metadata.unit_sha256, 65);
    source.has_profile = true;
    tired_environment_destroy(&source.environment);
    source.has_environment = false;
    tired_text_destroy(&source.environment_path);
    source.environment_revision[0] = source.environment_sha256[0] = '\0';
    CHECK(tired_service_record_encode(&source, &encoded, &error));
    CHECK(tired_service_record_parse(encoded.data, encoded.length, &parsed, &error));
    CHECK(parsed.has_profile && !parsed.has_environment &&
          strcmp(parsed.profile.profile.id, "generic") == 0);
    cwd = getcwd(NULL, 0);
    CHECK(cwd != NULL && (created = mkdtemp(fixture)) != NULL);
    TiredText base = {.data = cwd, .length = strlen(cwd)};
    CHECK(tired_path_absolute(&base, created, strlen(created), &directory_path, &error));
    CHECK(tired_directory_open(directory_path.data, getuid(), true, &directory, &error));
    CHECK(set(&layout.paths[TIRED_PATH_RECORDS], directory_path.data, &error));
    CHECK(set(&layout.paths[TIRED_PATH_UNITS], directory_path.data, &error));
    CHECK(tired_path_absolute(&directory_path, "relay.service", 13, &source.unit_path, &error));
    source.metadata.owner_uid = source.metadata.invoking_uid = source.metadata.service_uid =
        getuid();
    CHECK(tired_service_record_encode(&source, &encoded, &error));
    (void)snprintf(name, sizeof(name), "%s.json", source.metadata.service_uuid);
    (void)snprintf(other, sizeof(other), "%s.json", source.metadata.revision_uuid);
    CHECK(!tired_service_record_load(&layout, source.metadata.service_uuid, &parsed, &error));
    CHECK(error.status == TIRED_NOT_FOUND);
    CHECK(tired_service_inventory_load(&layout, &inventory, &error));
    CHECK(inventory.complete && inventory.count == 0);
    CHECK(!tired_service_inventory_find(&inventory, "relay.service", &found, &error) &&
          error.status == TIRED_NOT_FOUND);
    CHECK(tired_private_file_create(directory, name, encoded.data, encoded.length, &error));
    CHECK(tired_service_record_load(&layout, source.metadata.service_uuid, &parsed, &error));
    CHECK(parsed.metadata.owner_uid == getuid());
    size_t budget = encoded.length;
    CHECK(tired_service_record_read_budget(directory, &layout, source.metadata.service_uuid,
                                           &budget, &parsed, &error));
    CHECK(budget == 0);
    CHECK(!tired_service_record_read_budget(directory, &layout, source.metadata.service_uuid,
                                            &budget, &parsed, &error));
    CHECK(error.status == TIRED_RECOVERY_REQUIRED);
    CHECK(tired_service_inventory_load(&layout, &inventory, &error));
    CHECK(inventory.complete && inventory.count == 1);
    CHECK(tired_service_inventory_find(&inventory, "relay.service", &found, &error));
    CHECK(strcmp(found->service_uuid, source.metadata.service_uuid) == 0);
    int fd = tired_directory_fd(directory);
    const char unusual_name[] = "unexpected\n\xff";
    int unusual_fd = openat(fd, unusual_name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    CHECK(unusual_fd >= 0);
    CHECK(close(unusual_fd) == 0);
    CHECK(tired_service_inventory_load(&layout, &inventory, &error));
    CHECK(!inventory.complete && inventory.count == 2 &&
          inventory.entries[0].error.status == TIRED_OK &&
          inventory.entries[1].error.status == TIRED_RECOVERY_REQUIRED &&
          inventory.entries[1].filename.length == sizeof(unusual_name) - 1 &&
          memcmp(inventory.entries[1].filename.data, unusual_name, sizeof(unusual_name)) == 0);
    CHECK(unlinkat(fd, unusual_name, 0) == 0);
    CHECK(tired_service_inventory_load(&layout, &inventory, &error));
    TiredServiceInventoryEntry *saved_entries = inventory.entries;
    CHECK(fchmod(fd, 0755) == 0);
    CHECK(!tired_service_inventory_load(&layout, &inventory, &error));
    CHECK(inventory.entries == saved_entries && inventory.complete && inventory.count == 1);
    CHECK(fchmod(fd, 0700) == 0);
    CHECK(tired_private_file_create(directory, other, "{", 1, &error));
    CHECK(tired_service_inventory_load(&layout, &inventory, &error));
    CHECK(!inventory.complete && inventory.count == 2 &&
          inventory.entries[0].error.status == TIRED_OK &&
          inventory.entries[1].error.status != TIRED_OK);
    CHECK(!tired_service_inventory_find(&inventory, "relay.service", &found, &error) &&
          error.status == TIRED_RECOVERY_REQUIRED);
    CHECK(unlinkat(fd, other, 0) == 0);
    source.metadata.service_uuid[0] = '1';
    CHECK(tired_service_record_encode(&source, &again, &error));
    source.metadata.service_uuid[0] = '0';
    CHECK(tired_private_file_create(directory, other, again.data, again.length, &error));
    CHECK(tired_service_inventory_load(&layout, &inventory, &error));
    CHECK(!inventory.complete && inventory.entries[0].error.status == TIRED_CONFLICT &&
          inventory.entries[1].error.status == TIRED_CONFLICT);
    CHECK(unlinkat(fd, other, 0) == 0);
    CHECK(renameat(fd, name, fd, other) == 0);
    CHECK(!tired_service_record_load(&layout, source.metadata.revision_uuid, &parsed, &error));
    CHECK(error.status == TIRED_CONFLICT &&
          strcmp(parsed.metadata.service_uuid, source.metadata.service_uuid) == 0);
    CHECK(symlinkat(other, fd, name) == 0);
    CHECK(!tired_service_record_load(&layout, source.metadata.service_uuid, &parsed, &error));
    CHECK(unlinkat(fd, name, 0) == 0 && renameat(fd, other, fd, name) == 0);
    CHECK(fchmodat(fd, name, 0644, 0) == 0);
    CHECK(!tired_service_record_load(&layout, source.metadata.service_uuid, &parsed, &error));
    CHECK(fchmodat(fd, name, 0600, 0) == 0);
    CHECK(status_checks(directory, &directory_path, &source) == 0);
    CHECK(file_checks(&layout, &source, directory) == 0);
    CHECK(set(&layout.paths[TIRED_PATH_UNITS], "/wrong", &error));
    CHECK(!tired_service_record_load(&layout, source.metadata.service_uuid, &parsed, &error));
    CHECK(!tired_service_record_load(&layout, "../escape", &parsed, &error));
    result = 0;
cleanup:
    tired_service_inventory_destroy(&inventory);
    if (directory != NULL)
    {
        (void)fchmod(tired_directory_fd(directory), 0700);
        (void)unlinkat(tired_directory_fd(directory), "unexpected\n\xff", 0);
        char cleanup_name[42];
        (void)snprintf(cleanup_name, sizeof(cleanup_name), "%s.json", source.metadata.service_uuid);
        (void)unlinkat(tired_directory_fd(directory), cleanup_name, 0);
        (void)snprintf(cleanup_name, sizeof(cleanup_name), "%s.json",
                       source.metadata.revision_uuid);
        (void)unlinkat(tired_directory_fd(directory), cleanup_name, 0);
    }
    tired_directory_destroy(directory);
    if (created != NULL)
        (void)rmdir(created);
    free(cwd);
    tired_text_destroy(&directory_path);
    json_object_put(document);
    tired_service_record_destroy(&source);
    tired_service_record_destroy(&parsed);
    tired_layout_destroy(&layout);
    tired_text_destroy(&encoded);
    tired_text_destroy(&again);
    return result;
}
