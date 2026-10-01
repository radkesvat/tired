#define _GNU_SOURCE
#include "tired/build_identity.h"
#include "tired/helper.h"
#include "tired/payload.h"
#include "tired/portable.h"
#include "tired/process.h"
#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static bool open_cache(bool create, TiredDirectory **output, TiredError *error)
{
    static const char *const components[] = {"var", "cache", "tired", "helpers"};
    TiredDirectory *current = NULL;
    if (!tired_directory_open("/", 0, false, &current, error))
        return false;
    for (size_t i = 0; i < sizeof(components) / sizeof(components[0]); ++i)
    {
        TiredDirectory *next = NULL;
        bool ok =
            tired_directory_child_mode(current, components[i], create, false, 0755, &next, error);
        tired_directory_destroy(current);
        if (!ok)
            return false;
        current = next;
    }
    *output = current;
    return true;
}

static bool install(TiredError *error)
{
    if (getuid() != 0 || geteuid() != 0 || getgid() != getegid())
        return tired_error_set(error, TIRED_AUTHORIZATION, "helper-setup-authorization",
                               "Bundled helper setup requires administrator permission.", 0);
    TiredDirectory *directory = NULL;
    bool ok = open_cache(true, &directory, error) &&
              tired_helper_cache_install(directory, &tired_embedded_helper[0], error);
    tired_directory_destroy(directory);
    return ok;
}

int tired_portable_setup(void)
{
    TiredError error = {0};
    if (!install(&error))
    {
        fprintf(stderr, "tired: %s [%s]\n", error.message, error.code);
        return error.status;
    }
    return TIRED_OK;
}

static bool installed_matches(const char *path)
{
    TiredError error = {0};
    if (!tired_helper_path_trusted(path, false, &error))
        return false;
    char *arguments[] = {(char *)path, "--build-id", NULL};
    char *environment[] = {"PATH=/usr/bin:/bin", "LC_ALL=C", NULL};
    TiredProcess *process = NULL;
    if (!tired_process_start(path, arguments, environment, 5000, 512, &process, &error))
        return false;
    struct timespec delay = {.tv_nsec = 10000000};
    while (!tired_process_step(process))
        (void)nanosleep(&delay, NULL);
    TiredProcessResult result = tired_process_result(process);
    size_t length = strlen(tired_build_identity);
    bool matches = result.outcome == TIRED_PROCESS_EXITED && result.exit_code == 0 &&
                   result.output_length == length + 1 &&
                   memcmp(result.standard_output, tired_build_identity, length) == 0 &&
                   result.standard_output[length] == '\n';
    tired_process_destroy(process);
    return matches;
}

static bool authorize_setup(bool interactive, TiredError *error)
{
    if (getuid() == 0)
        return install(error);
    if (!tired_helper_path_trusted("/usr/bin/sudo", true, error))
        return false;
    /* Pin the executable that is already running, even when the downloaded path
     * is renamed or replaced during the password prompt. No caller path reaches
     * the elevated entry point, and sudo retains its ordinary authorization. */
    char executable[80];
    (void)snprintf(executable, sizeof(executable), "/proc/%ld/exe", (long)getpid());
    char *arguments[] = {"/usr/bin/sudo",
                         interactive ? "--" : "-n",
                         interactive ? executable : "--",
                         interactive ? "--internal-install-helper" : executable,
                         interactive ? NULL : "--internal-install-helper",
                         NULL};
    char *environment[] = {"PATH=/usr/bin:/bin", "LC_ALL=C", NULL};
    if (interactive)
        fputs("Setting up tired's bundled helper. Administrator permission is required.\n", stderr);
    pid_t child;
    int rc = posix_spawn(&child, "/usr/bin/sudo", NULL, NULL, arguments, environment);
    if (rc != 0)
        return tired_error_set(error, TIRED_AUTHORIZATION, "helper-setup-start",
                               "Cannot start bundled helper setup.", rc);
    int status = 0;
    pid_t waited;
    do
        waited = waitpid(child, &status, 0);
    while (waited < 0 && errno == EINTR);
    if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return tired_error_set(error, TIRED_AUTHORIZATION, "helper-setup-failed",
                               "Bundled helper setup did not complete. Run from a terminal with "
                               "administrator permission, or install the complete package.",
                               0);
    tired_error_clear(error);
    return true;
}

bool tired_portable_prepare_helper(bool interactive, TiredText *path, TiredError *error)
{
    if (getuid() != geteuid() || getgid() != getegid())
        return tired_error_set(error, TIRED_AUTHORIZATION, "helper-setup-identity",
                               "tired must not run as a setuid or setgid executable.", 0);
    TiredText installed = {0};
    if (!tired_payload_path(true, &installed, error))
        return false;
    if (installed_matches(installed.data))
    {
        tired_text_destroy(path);
        *path = installed;
        tired_error_clear(error);
        return true;
    }
    tired_text_destroy(&installed);
    TiredDirectory *directory = NULL;
    bool found = false;
    if (open_cache(false, &directory, error))
    {
        bool ok = tired_helper_cache_find(directory, &tired_embedded_helper[0], &found, error);
        tired_directory_destroy(directory);
        directory = NULL;
        if (!ok)
            return false;
    }
    else if (error->status != TIRED_NOT_FOUND)
        return false;
    if (!found)
    {
        if (!authorize_setup(interactive, error) || !open_cache(false, &directory, error))
            return false;
        bool ok = tired_helper_cache_find(directory, &tired_embedded_helper[0], &found, error);
        tired_directory_destroy(directory);
        if (!ok || !found)
            return tired_error_set(error, TIRED_RUNTIME_FAILED, "helper-setup-missing",
                                   "The matching bundled helper is unavailable after setup.", 0);
    }
    char cached[256];
    int length = snprintf(cached, sizeof(cached), "%s/%s/tired-helper", TIRED_HELPER_CACHE,
                          tired_embedded_helper[0].sha256);
    return length > 0 && (size_t)length < sizeof(cached) &&
           tired_text_set(path, cached, (size_t)length, 4096, error);
}
