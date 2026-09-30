#define _GNU_SOURCE
#include "tired/helper.h"
#include "tired/identity.h"
#include "tired/limit.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
int main(void)
{
    closefrom(3);
    if (getuid() != 0 || geteuid() != 0 || getgid() != getegid())
    {
        fputs("tired-helper requires administrator authorization.\n", stderr);
        return TIRED_AUTHORIZATION;
    }
    uid_t actor_uid = getuid();
    const char *uid_hint = getenv("SUDO_UID"), *name_hint = getenv("SUDO_USER");
    if (uid_hint != NULL || name_hint != NULL)
    {
        uint64_t uid;
        TiredAccount account = {0};
        TiredError hint_error = {0};
        bool matched =
            uid_hint != NULL && name_hint != NULL &&
            tired_parse_u64(uid_hint, strlen(uid_hint), 0, UINT32_MAX - 1, &uid, &hint_error) &&
            tired_account_resolve(name_hint, strlen(name_hint), &account, &hint_error) &&
            account.uid == (uid_t)uid;
        if (matched)
            actor_uid = (uid_t)uid;
        tired_account_destroy(&account);
        if (!matched)
            return TIRED_AUTHORIZATION;
    }
    (void)umask(0077);
    (void)signal(SIGPIPE, SIG_IGN);
    TiredText request = {0}, output = {0};
    TiredError error = {0};
    int status = TIRED_INTERNAL;
    if (!tired_protocol_ready(STDOUT_FILENO, &error) ||
        !tired_protocol_read(STDIN_FILENO, 10000, &request, &error))
        goto failed;
    /* Environment hints are never an authority source. The root administrative
     * process already has unrestricted authority; all destinations are derived. */
    if (clearenv() != 0 || setenv("PATH", "/usr/bin:/bin", 1) != 0 ||
        setenv("LC_ALL", "C", 1) != 0 || chdir("/") != 0)
    {
        tired_error_set(&error, TIRED_INTERNAL, "helper-environment",
                        "Cannot sanitize helper context.", errno);
        goto failed;
    }
    (void)signal(SIGHUP, SIG_IGN);
    (void)signal(SIGINT, SIG_IGN);
    (void)signal(SIGTERM, SIG_IGN);
    (void)setsid();
    (void)signal(SIGALRM, SIG_DFL);
    (void)alarm(600);
    if (!tired_helper_dispatch(&request, false, false, actor_uid, &output, &error))
        goto failed;
    status = TIRED_OK;
    goto reply;
failed:
    status = error.status == TIRED_OK ? TIRED_INTERNAL : error.status;
    (void)tired_helper_error_output(&error, &output, &error);
reply:
    /* The transaction is already complete or durably recoverable. EPIPE changes
     * delivery only; it must never undo or abandon the approved operation. */
    (void)tired_protocol_write(STDOUT_FILENO, &output, 5000, &error);
    tired_text_destroy(&request);
    tired_text_destroy(&output);
    return status;
}
