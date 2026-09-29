#include "tired/config_frontend.h"
#include "tired/journal_access.h"
#include "tired/journal_output.h"
#include "tired/journal_selection.h"
#include "tired/journal_stream.h"
#include "tired/logs_frontend.h"
#include "tired/name.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <json-c/json.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct
{
    int fd;
    volatile sig_atomic_t *cancel;
} LogOutput;
static bool write_event(LogOutput *output, const char *text, size_t length, TiredError *error)
{
    size_t offset = 0;
    while (offset < length)
    {
        if (*output->cancel)
            return tired_error_set(error, TIRED_INTERRUPTED, "logs-output-write",
                                   "Journal output interrupted.", EINTR);
        ssize_t written = write(output->fd, text + offset, length - offset);
        if (written > 0)
        {
            offset += (size_t)written;
            continue;
        }
        if (written < 0 && errno == EINTR)
            continue;
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            struct pollfd descriptor = {.fd = output->fd, .events = POLLOUT};
            int rc = poll(&descriptor, 1, 1000);
            if (rc >= 0 && !(descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)))
                continue;
            if (rc < 0 && errno == EINTR)
                continue;
            if (rc >= 0)
                errno = EIO;
        }
        return tired_error_set(error, TIRED_RUNTIME_FAILED, "logs-output-write",
                               "Cannot write journal output.", written == 0 ? EIO : errno);
    }
    return true;
}
static bool add(struct json_object *object, const char *key, struct json_object *value)
{
    if (value == NULL)
        return false;
    if (json_object_object_add(object, key, value) == 0)
        return true;
    json_object_put(value);
    return false;
}
static bool event(LogOutput *out, bool json, const char *type, const char *unit, bool user,
                  const TiredJournalAccess *access, uint64_t records, TiredStatus status,
                  TiredError *error)
{
    if (!json)
    {
        char line[768];
        if (access != NULL)
            (void)snprintf(
                line, sizeof(line),
                "%s (%s): journal access %s; %zu/%zu files opened, %zu storage roots absent.%s%s\n",
                unit, user ? "user" : "system", access->complete ? "checked" : "limited",
                access->files_opened, access->files_seen, access->missing_roots,
                access->first_issue.message != NULL ? " " : "",
                access->first_issue.message != NULL ? access->first_issue.message : "");
        else if (strcmp(type, "journal_invalidate") == 0)
            (void)snprintf(line, sizeof(line),
                           "%s (%s): journal files changed; earlier entries may have appeared or "
                           "disappeared.\n",
                           unit, user ? "user" : "system");
        else
            (void)snprintf(
                line, sizeof(line),
                "%s (%s): displayed %" PRIu64
                " records from accessible journals; content completeness is not guaranteed.\n",
                unit, user ? "user" : "system", records);
        return write_event(out, line, strlen(line), error);
    }
    struct json_object *object = json_object_new_object();
    bool ok = object != NULL && add(object, "schema_version", json_object_new_int(1)) &&
              add(object, "event_type", json_object_new_string(type)) &&
              add(object, "selected_service", json_object_new_string(unit)) &&
              add(object, "scope", json_object_new_string(user ? "user" : "system")) &&
              (!user || add(object, "selected_uid", json_object_new_uint64(getuid()))) &&
              add(object, "content_completeness", json_object_new_string("not_guaranteed"));
    if (ok && access != NULL)
        ok = add(object, "access_checked", json_object_new_boolean(access->complete)) &&
             add(object, "files_seen", json_object_new_uint64(access->files_seen)) &&
             add(object, "files_opened", json_object_new_uint64(access->files_opened)) &&
             add(object, "missing_roots", json_object_new_uint64(access->missing_roots)) &&
             add(object, "issues", json_object_new_uint64(access->issues)) &&
             (access->first_issue.code == NULL ||
              add(object, "first_issue", json_object_new_string(access->first_issue.code)));
    if (ok && strcmp(type, "journal_end") == 0)
        ok = add(object, "records", json_object_new_uint64(records)) &&
             add(object, "exit_code", json_object_new_int(status));
    const char *text = ok ? json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN) : NULL;
    if (text == NULL)
        ok = tired_error_set(error, TIRED_INTERNAL, "allocation",
                             "Cannot construct journal diagnostic event.", 0);
    else
        ok = write_event(out, text, strlen(text), error) && write_event(out, "\n", 1, error);
    json_object_put(object);
    return ok;
}
static bool access_event(const TiredRequest *request, const char *const roots[2],
                         const char *machine, const char *unit, bool user, LogOutput *output,
                         TiredStatus *status, TiredError *error)
{
    TiredJournalAccess access;
    if (!tired_journal_access_check(roots, machine, user, getuid(), &access, error))
        return false;
    if (!access.complete)
        *status = TIRED_RECOVERY_REQUIRED;
    return event(output, request->json, "journal_access", unit, user, &access, 0, *status, error);
}
bool tired_logs_session(const TiredRequest *request, sd_journal *journal,
                        const char *const roots[2], const char *machine_id, size_t default_lines,
                        FILE *output, volatile sig_atomic_t *cancel, TiredStatus *result,
                        TiredError *error)
{
    assert(request != NULL && journal != NULL && machine_id != NULL && output != NULL &&
           cancel != NULL && result != NULL);
    if (request->command != TIRED_COMMAND_LOGS || request->arguments.count != 1 ||
        request->output.data != NULL || request->unit || request->include_sensitive)
        return tired_error_set(
            error, TIRED_INVALID, "logs-command",
            "Expected logs NAME with journal query options; file export is not supported.", 0);
    if (getuid() != geteuid() || getgid() != getegid())
        return tired_error_set(error, TIRED_AUTHORIZATION, "logs-identity",
                               "Journal reads require matching real and effective identities.", 0);
    bool user = tired_spec_choice_is(&request->overrides, TIRED_FIELD_SCOPE, "user");
    TiredText base = {0}, unit = {0}, rendered = {0};
    TiredJournalRecord record = {0};
    TiredJournalSelection selection;
    TiredJournalStream stream = {0};
    TiredStatus status = TIRED_OK;
    uint64_t records = 0;
    bool ok = false;
    LogOutput writer = {.fd = fileno(output), .cancel = cancel};
    int output_flags = fcntl(writer.fd, F_GETFL);
    bool flags_changed = false;
    if (output_flags < 0 || fflush(output) != 0 ||
        fcntl(writer.fd, F_SETFL, output_flags | O_NONBLOCK) < 0)
    {
        tired_error_set(error, TIRED_RUNTIME_FAILED, "logs-output-write",
                        "Cannot prepare journal output.", errno);
        goto done;
    }
    flags_changed = true;
    char current_boot[33];
    const char *boot = request->logs.boot_set ? request->logs.boot_id : NULL;
    if (request->logs.boot_current)
    {
        sd_id128_t id;
        int rc = sd_id128_get_boot(&id);
        if (rc < 0)
        {
            tired_error_set(error, TIRED_RUNTIME_FAILED, "logs-boot-read",
                            "Cannot determine the current boot ID.", -rc);
            goto done;
        }
        boot = sd_id128_to_string(id, current_boot);
    }
    if (!tired_name_explicit(request->arguments.items[0].data, request->arguments.items[0].length,
                             &base, error) ||
        !tired_name_candidate(&base, 1, &unit, error) ||
        !tired_journal_selection_build(unit.data, user, getuid(), boot, &selection, error) ||
        !tired_journal_selection_apply(journal, &selection, error) ||
        !access_event(request, roots, machine_id, unit.data, user, &writer, &status, error) ||
        !tired_journal_stream_native(&stream, journal,
                                     request->logs.lines_set ? request->logs.lines : default_lines,
                                     request->logs.since_set, request->logs.since_usec, error))
        goto done;
    /* Establish notification watches before draining the initial tail. */
    TiredJournalPoll watch;
    if (request->logs.follow && !tired_journal_stream_poll(&stream, &watch, error))
        goto done;
    while (!*cancel)
    {
        TiredJournalStep step =
            tired_journal_stream_next(&stream, request->logs.follow, &record, error);
        if (step == TIRED_JOURNAL_ERROR)
            goto done;
        if (step == TIRED_JOURNAL_RECORD)
        {
            if (!tired_journal_output(&record, unit.data, user, getuid(), request->json, &rendered,
                                      error) ||
                !write_event(&writer, rendered.data, rendered.length, error))
                goto done;
            ++records;
            tired_text_destroy(&rendered);
            tired_journal_record_destroy(&record);
            continue;
        }
        if (step == TIRED_JOURNAL_SKIPPED)
            continue;
        if (!request->logs.follow)
            break;
        if (!tired_journal_stream_poll(&stream, &watch, error))
            goto done;
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        {
            tired_error_set(error, TIRED_RUNTIME_FAILED, "logs-clock",
                            "Cannot read the journal polling clock.", errno);
            goto done;
        }
        uint64_t current = (uint64_t)now.tv_sec * 1000000U + (uint64_t)now.tv_nsec / 1000U;
        int timeout = 1000; /* Also bounds the cancellation race before poll. */
        if (watch.monotonic_deadline_usec != UINT64_MAX)
        {
            uint64_t remaining = watch.monotonic_deadline_usec > current
                                     ? watch.monotonic_deadline_usec - current
                                     : 0;
            if (remaining < 1000000U)
                timeout = (int)((remaining + 999U) / 1000U);
        }
        struct pollfd descriptor = {.fd = watch.fd, .events = (short)watch.events};
        int rc = poll(&descriptor, 1, timeout);
        if (*cancel)
            break;
        if (rc < 0 && errno == EINTR)
            continue;
        if (rc < 0 || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)))
        {
            tired_error_set(error, TIRED_RUNTIME_FAILED, "logs-poll", "Journal polling failed.",
                            rc < 0 ? errno : EIO);
            goto done;
        }
        rc = tired_journal_stream_process(&stream, error);
        if (rc < 0)
            goto done;
        if (rc == SD_JOURNAL_INVALIDATE &&
            (!event(&writer, request->json, "journal_invalidate", unit.data, user, NULL, records,
                    status, error) ||
             !access_event(request, roots, machine_id, unit.data, user, &writer, &status, error)))
            goto done;
    }
    if (*cancel)
        status = TIRED_INTERRUPTED;
    else if (!access_event(request, roots, machine_id, unit.data, user, &writer, &status, error) ||
             !event(&writer, request->json, "journal_end", unit.data, user, NULL, records, status,
                    error))
        goto done;
    *result = status;
    tired_error_clear(error);
    ok = true;
done:
    if (*cancel && error->code != NULL && strcmp(error->code, "logs-output-write") != 0)
    {
        *result = TIRED_INTERRUPTED;
        tired_error_clear(error);
        ok = true;
    }
    if (flags_changed && fcntl(writer.fd, F_SETFL, output_flags) < 0)
        ok = tired_error_set(error, TIRED_RUNTIME_FAILED, "logs-output-write",
                             "Cannot restore journal output flags.", errno);
    tired_journal_record_destroy(&record);
    tired_text_destroy(&rendered);
    tired_text_destroy(&base);
    tired_text_destroy(&unit);
    return ok;
}
bool tired_logs_command(const TiredRequest *request, FILE *output, volatile sig_atomic_t *cancel,
                        TiredStatus *result, TiredError *error)
{
    TiredSettings settings = {0};
    sd_journal *journal = NULL;
    bool ok = false;
    if (!tired_config_discover(request, &settings, error))
        goto done;
    sd_id128_t machine;
    int rc = sd_id128_get_machine(&machine);
    if (rc < 0)
    {
        tired_error_set(error, TIRED_RUNTIME_FAILED, "logs-machine",
                        "Cannot determine the local machine ID.", -rc);
        goto done;
    }
    bool user = tired_spec_choice_is(&request->overrides, TIRED_FIELD_SCOPE, "user");
    rc = sd_journal_open(&journal, SD_JOURNAL_LOCAL_ONLY |
                                       (user ? SD_JOURNAL_SYSTEM | SD_JOURNAL_CURRENT_USER : 0));
    if (rc < 0)
    {
        tired_error_set(error,
                        rc == -EACCES || rc == -EPERM ? TIRED_AUTHORIZATION : TIRED_RUNTIME_FAILED,
                        "logs-open", "Cannot open local journal storage.", -rc);
        goto done;
    }
    char machine_id[33];
    sd_id128_to_string(machine, machine_id);
    ok = tired_logs_session(request, journal, NULL, machine_id, (size_t)settings.log_tail, output,
                            cancel, result, error);
done:
    sd_journal_close(journal);
    tired_settings_destroy(&settings);
    return ok;
}
