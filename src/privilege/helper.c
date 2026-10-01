#define _GNU_SOURCE
#include "tired/helper.h"
#include "tired/identity.h"
#include "tired/io.h"
#include "tired/json.h"
#include "tired/name.h"
#include "tired/payload.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifndef TIRED_HELPER_PATH
#error The installed helper path is required.
#endif
static volatile sig_atomic_t interrupted, submitted, announce;
static void handle_signal(int number)
{
    interrupted = number;
    if (submitted && announce)
    {
        const char message[] =
            "\nThe approved operation continues in its worker; waiting for the recorded result.\n";
        (void)write(STDERR_FILENO, message, sizeof(message) - 1);
        announce = 0;
    }
}
bool tired_helper_path_trusted(const char *path, bool sudo_tool, TiredError *error)
{
    char current[4096];
    size_t length = strlen(path);
    if (length >= sizeof(current) || path[0] != '/')
        return false;
    memcpy(current, path, length + 1);
    for (char *position = current + 1;; ++position)
        if (*position == '/' || *position == '\0')
        {
            char saved = *position;
            *position = '\0';
            struct stat file;
            bool ok = lstat(current, &file) == 0 && file.st_uid == 0;
            if (ok && saved != '\0' && S_ISLNK(file.st_mode))
                ok = stat(current, &file) == 0 && file.st_uid == 0;
            ok = ok && (file.st_mode & 0022) == 0 &&
                 (saved == '\0'
                      ? S_ISREG(file.st_mode) && (sudo_tool || (file.st_mode & 06000) == 0) &&
                            (file.st_mode & 0111) != 0
                      : S_ISDIR(file.st_mode));
            *position = saved;
            if (!ok)
                return tired_error_set(error, TIRED_AUTHORIZATION, "helper-path-trust",
                                       "Install the administrator-owned helper under a trusted "
                                       "prefix before system mutations.",
                                       errno);
            if (saved == '\0')
                break;
        }
    /* Preserve and check the original ancestry before resolving administrator
     * aliases such as /snap. A writable/user-owned link can never borrow the
     * target's trust. The resolved ancestry receives the same checks. */
    char *resolved = realpath(path, NULL);
    if (resolved == NULL)
        return tired_error_set(error, TIRED_AUTHORIZATION, "helper-path-resolution",
                               "Cannot resolve the trusted helper path.", errno);
    bool ok = strcmp(resolved, path) == 0 || tired_helper_path_trusted(resolved, sudo_tool, error);
    free(resolved);
    return ok;
}
static bool call_request(const TiredText *request, bool user, bool interactive, TiredText *output,
                         TiredStatus *status, TiredError *error)
{
    TiredText helper_path = {0};
    if (!user && !tired_payload_prepare_helper(interactive, &helper_path, error))
        return false;
    if (!user && (!tired_helper_path_trusted(helper_path.data, false, error) ||
                  (getuid() != 0 && !tired_helper_path_trusted("/usr/bin/sudo", true, error))))
    {
        tired_text_destroy(&helper_path);
        return false;
    }
    const int signals[] = {SIGINT, SIGTERM, SIGHUP};
    struct sigaction old[3], action = {.sa_handler = handle_signal};
    sigemptyset(&action.sa_mask);
    size_t handlers = 0;
    interrupted = submitted = 0;
    announce = interactive;
    TiredText response = {0}, ready = {0};
    int input[2] = {-1, -1}, result[2] = {-1, -1};
    pid_t child = -1;
    bool ok = false;
    for (; handlers < 3; ++handlers)
        if (sigaction(signals[handlers], &action, &old[handlers]) != 0)
            goto done;
    if (pipe2(input, O_CLOEXEC | O_NONBLOCK) != 0 || pipe2(result, O_CLOEXEC | O_NONBLOCK) != 0)
        goto done;
    if (user)
    {
        child = fork();
        if (child == 0)
        {
            int read_fd = fcntl(input[0], F_DUPFD_CLOEXEC, 5);
            int write_fd = fcntl(result[1], F_DUPFD_CLOEXEC, 5);
            if (read_fd < 0 || write_fd < 0 || dup2(read_fd, 3) < 0 || dup2(write_fd, 4) < 0)
                _exit(TIRED_INTERNAL);
            closefrom(5);
            input[0] = 3;
            result[1] = 4;
            int null = open("/dev/null", O_RDWR | O_CLOEXEC);
            if (null < 0 || dup2(null, STDIN_FILENO) < 0 || dup2(null, STDOUT_FILENO) < 0)
                _exit(TIRED_INTERNAL);
            if (null > 4)
                close(null);
            (void)signal(SIGPIPE, SIG_IGN);
            (void)signal(SIGHUP, SIG_IGN);
            (void)signal(SIGINT, SIG_IGN);
            (void)signal(SIGTERM, SIG_IGN);
            /* Retain the controlling terminal until explicit account-change
             * authentication finishes. Signals cannot abandon the approved worker. */
            (void)signal(SIGALRM, SIG_DFL);
            (void)alarm(600);
            TiredText received = {0}, encoded = {0};
            TiredError failure = {0};
            bool completed =
                tired_protocol_ready(result[1], &failure) &&
                tired_protocol_read(input[0], 10000, &received, &failure) &&
                tired_helper_dispatch(&received, true, interactive, getuid(), &encoded, &failure);
            if (!completed)
                (void)tired_helper_error_output(&failure, &encoded, &failure);
            (void)tired_protocol_write(result[1], &encoded, 5000, &failure);
            tired_text_destroy(&received);
            tired_text_destroy(&encoded);
            _exit(0);
        }
        if (child < 0)
            goto done;
    }
    else
    {
        posix_spawn_file_actions_t actions;
        int rc = posix_spawn_file_actions_init(&actions);
        if (rc != 0)
            goto done;
        rc = posix_spawn_file_actions_adddup2(&actions, input[0], STDIN_FILENO);
        if (rc == 0)
            rc = posix_spawn_file_actions_adddup2(&actions, result[1], STDOUT_FILENO);
        if (rc == 0)
            rc = posix_spawn_file_actions_addclosefrom_np(&actions, 3);
        TiredAccount origin = {0};
        char uid_hint[64], name_hint[512];
        char *environment[] = {"PATH=/usr/bin:/bin", "LC_ALL=C", NULL, NULL, NULL};
        if (getuid() == 0)
        {
            if (!tired_invoking_account(false, &origin, error))
            {
                posix_spawn_file_actions_destroy(&actions);
                goto done;
            }
            int uid_length =
                snprintf(uid_hint, sizeof(uid_hint), "SUDO_UID=%lu", (unsigned long)origin.uid);
            int name_length =
                snprintf(name_hint, sizeof(name_hint), "SUDO_USER=%s", origin.name.data);
            tired_account_destroy(&origin);
            if (uid_length < 0 || (size_t)uid_length >= sizeof(uid_hint) || name_length < 0 ||
                (size_t)name_length >= sizeof(name_hint))
            {
                posix_spawn_file_actions_destroy(&actions);
                goto done;
            }
            environment[2] = uid_hint;
            environment[3] = name_hint;
        }
        char *arguments[] = {"/usr/bin/sudo", interactive ? "--" : "-n",
                             interactive ? helper_path.data : "--",
                             interactive ? NULL : helper_path.data, NULL};
        char *root_arguments[] = {helper_path.data, NULL};
        if (rc == 0)
            rc = posix_spawn(&child, getuid() == 0 ? helper_path.data : "/usr/bin/sudo", &actions,
                             NULL, getuid() == 0 ? root_arguments : arguments, environment);
        posix_spawn_file_actions_destroy(&actions);
        if (rc != 0)
        {
            tired_error_set(error, TIRED_AUTHORIZATION, "helper-spawn",
                            "Cannot start the administrative helper.", rc);
            goto done;
        }
    }
    (void)close(input[0]);
    input[0] = -1;
    (void)close(result[1]);
    result[1] = -1;
    if (!tired_protocol_read_interruptible(result[0], interactive ? 300000 : 10000, &interrupted,
                                           &ready, error))
    {
        if (error->status != TIRED_INTERRUPTED)
            error->status = user ? TIRED_INTERNAL : TIRED_AUTHORIZATION;
        goto done;
    }
    if (!tired_protocol_check_ready(&ready, error))
        goto done;
    if (!tired_protocol_write_interruptible(input[1], request, 10000, &interrupted, error))
        goto done;
    submitted = 1;
    if (interactive)
        fputs("Applying the approved operation. The worker will finish or leave a recovery journal "
              "if this session closes.\n",
              stderr);
    (void)close(input[1]);
    input[1] = -1;
    if (!tired_protocol_read(result[0], 600000, &response, error))
    {
        error->status = TIRED_RECOVERY_REQUIRED;
        goto done;
    }
    struct json_object *document = NULL, *code = NULL;
    uint64_t number;
    ok = tired_json_parse(response.data, response.length, TIRED_INPUT_LIMIT, &document, error) &&
         json_object_object_get_ex(document, "exit_code", &code) &&
         tired_json_u64(code, 0, 130, &number, error) && (number <= 10 || number == 130);
    if (ok)
    {
        *status = (TiredStatus)number;
        tired_text_destroy(output);
        *output = response;
        response = (TiredText){0};
    }
    json_object_put(document);
done:
    if (!ok && submitted)
        error->status = TIRED_RECOVERY_REQUIRED;
    if (!submitted && child > 0)
        (void)kill(child, SIGTERM);
    while (handlers != 0)
    {
        --handlers;
        (void)sigaction(signals[handlers], &old[handlers], NULL);
    }
    for (size_t i = 0; i < 2; ++i)
    {
        if (input[i] >= 0)
            (void)close(input[i]);
        if (result[i] >= 0)
            (void)close(result[i]);
    }
    /* A complete approved request belongs to the bounded worker. Closing its
     * display channel never kills it or cancels an already submitted job. */
    if (child > 0)
    {
        int state;
        if (ok)
            while (waitpid(child, &state, 0) < 0 && errno == EINTR)
            {
            }
        else
            (void)waitpid(child, &state, WNOHANG);
    }
    tired_text_destroy(&response);
    tired_text_destroy(&ready);
    tired_text_destroy(&helper_path);
    if (!ok && error->status == TIRED_OK)
        tired_error_set(error, TIRED_INTERNAL, "helper-ipc", "Cannot complete administrative IPC.",
                        errno);
    return ok;
}

bool tired_helper_call(const TiredMutation *mutation, bool interactive, TiredText *output,
                       TiredStatus *status, TiredError *error)
{
    TiredText request = {0};
    bool ok = tired_mutation_encode(mutation, &request, error) &&
              call_request(&request, mutation->proposed.metadata.user_scope, interactive, output,
                           status, error);
    tired_text_destroy(&request);
    return ok;
}
bool tired_helper_recover_call(bool user, const char *uuid, bool finish, bool interactive,
                               TiredText *output, TiredStatus *status, TiredError *error)
{
    if (!tired_uuid_valid(uuid, strlen(uuid)))
        return tired_error_set(error, TIRED_INVALID, "recovery-id",
                               "Expected a canonical transaction UUID.", 0);
    char bytes[256];
    int length = snprintf(bytes, sizeof(bytes),
                          "{\"protocol_version\":1,\"kind\":\"recover\",\"user_scope\":%s,"
                          "\"transaction_uuid\":\"%s\",\"finish\":%s}",
                          user ? "true" : "false", uuid, finish ? "true" : "false");
    TiredText request = {.data = bytes, .length = length > 0 ? (size_t)length : 0};
    return length > 0 && (size_t)length < sizeof(bytes) &&
           call_request(&request, user, interactive, output, status, error);
}

bool tired_helper_linger_call(uid_t uid, bool interactive, bool *uncertain, TiredError *error)
{
    char bytes[160];
    int length = snprintf(bytes, sizeof(bytes),
                          "{\"protocol_version\":1,\"kind\":\"enable_linger\",\"uid\":%lu}",
                          (unsigned long)uid);
    TiredText request = {.data = bytes, .length = length > 0 ? (size_t)length : 0}, response = {0};
    TiredStatus status = TIRED_OK;
    bool ok = length > 0 && (size_t)length < sizeof(bytes) &&
              call_request(&request, false, interactive, &response, &status, error);
    *uncertain = (!ok && error->status == TIRED_RECOVERY_REQUIRED) ||
                 (ok && status == TIRED_RECOVERY_REQUIRED);
    if (ok && status != TIRED_OK)
        ok = tired_error_set(error, status, "linger-authorization",
                             "The requested account change was not confirmed. Inspect lingering "
                             "and the current recovery transaction.",
                             0);
    tired_text_destroy(&response);
    return ok;
}

bool tired_helper_record_call(const char *name, bool hydrate, bool interactive,
                              TiredMutation *mutation, TiredError *error)
{
    TiredText base = {0}, unit = {0}, output = {0};
    if (!tired_name_explicit(name, strlen(name), &base, error) ||
        !tired_name_candidate(&base, 1, &unit, error))
    {
        tired_text_destroy(&base);
        tired_text_destroy(&unit);
        return false;
    }
    char bytes[512];
    int length = snprintf(
        bytes, sizeof(bytes),
        "{\"protocol_version\":1,\"kind\":\"service_record\",\"unit_name\":\"%s\",\"hydrate\":%s}",
        unit.data, hydrate ? "true" : "false");
    TiredText request = {.data = bytes, .length = length > 0 ? (size_t)length : 0};
    TiredStatus status = TIRED_OK;
    bool ok = length > 0 && (size_t)length < sizeof(bytes) &&
              call_request(&request, false, interactive, &output, &status, error);
    if (ok && status != TIRED_OK)
        ok = tired_error_set(
            error, status, "record-inspection",
            "Cannot inspect the requested managed service under administrator authority.", 0);
    if (ok)
    {
        struct json_object *document = NULL, *record = NULL;
        ok = tired_json_parse(output.data, output.length, TIRED_INPUT_LIMIT, &document, error) &&
             json_object_object_get_ex(document, "record", &record);
        const char *encoded =
            ok ? json_object_to_json_string_ext(record, JSON_C_TO_STRING_PLAIN) : NULL;
        ok = encoded != NULL && tired_mutation_parse(encoded, strlen(encoded), mutation, error);
        json_object_put(document);
    }
    tired_text_destroy(&base);
    tired_text_destroy(&unit);
    tired_text_destroy(&output);
    return ok;
}
