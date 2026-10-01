#define _GNU_SOURCE
#include "tired/build_identity.h"
#include "tired/io.h"
#include "tired/payload.h"
#include "tired/portable.h"
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(e)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(e))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%d: %s [%s]\n", __LINE__, #e,                                         \
                    error.code == NULL ? "none" : error.code);                                     \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)

typedef struct
{
    const char *root;
    bool ok;
} RuntimeCopy;

static bool copy_file(const char *root, const char *source, const char *destination)
{
    char path[4096];
    int length = snprintf(path, sizeof(path), "%s%s", root, destination);
    if (length < 0 || (size_t)length >= sizeof(path))
        return false;
    for (char *p = path + strlen(root) + 1; *p != '\0'; ++p)
        if (*p == '/')
        {
            *p = '\0';
            int rc = mkdir(path, 0755), saved = errno;
            *p = '/';
            if (rc != 0 && saved != EEXIST)
                return false;
        }
    TiredText bytes = {0};
    TiredError error = {0};
    bool ok = tired_read_file(source, 128U * 1024U * 1024U, &bytes, &error) &&
              tired_write_private_new(path, bytes.data, bytes.length, &error) &&
              chmod(path, 0755) == 0;
    tired_text_destroy(&bytes);
    return ok;
}

static int copy_runtime(struct dl_phdr_info *info, size_t size, void *context)
{
    (void)size;
    RuntimeCopy *copy = context;
    if (info->dlpi_name[0] == '/' && !copy_file(copy->root, info->dlpi_name, info->dlpi_name))
        copy->ok = false;
    return 0;
}

static bool child(const char *root, bool nonroot, bool prepare, char *const arguments[],
                  int expected)
{
    pid_t pid = fork();
    if (pid == 0)
    {
        if (chroot(root) != 0 || chdir("/") != 0 ||
            (nonroot && (setgid(65534) != 0 || setuid(65534) != 0)))
            _exit(125);
        if (prepare)
        {
            TiredText path = {0};
            TiredError error = {0};
            bool ok = tired_portable_prepare_helper(false, &path, &error);
            if (!ok)
                fprintf(stderr, "portable preparation: %s [%s]\n", error.message, error.code);
            tired_text_destroy(&path);
            _exit(ok ? 0 : 1);
        }
        char *environment[] = {"PATH=/usr/bin:/bin", "LC_ALL=C", NULL};
        execve(arguments[0], arguments, environment);
        _exit(126);
    }
    if (pid < 0)
        return false;
    int status;
    return waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == expected;
}

static int remove_entry(const char *path, const struct stat *status, int kind, struct FTW *walk)
{
    (void)status;
    (void)kind;
    (void)walk;
    return remove(path);
}

int main(int argc, char **argv)
{
    if (getuid() != 0)
        return 77;
    int result = 1;
    char fixture[] = "portable-test-XXXXXX";
    char *created = mkdtemp(fixture), *root = NULL;
    TiredError error = {0};
    char path[4096], cached[256];
    TiredText bytes = {0}, installed = {0};
    CHECK(argc == 3 && created != NULL && (root = realpath(created, NULL)) != NULL);
    CHECK(copy_file(root, argv[1], "/tired"));
    RuntimeCopy runtime = {.root = root, .ok = true};
    (void)dl_iterate_phdr(copy_runtime, &runtime);
    CHECK(runtime.ok);
    CHECK(copy_file(root, "/etc/passwd", "/etc/passwd"));
    CHECK(copy_file(root, "/etc/group", "/etc/group"));
    CHECK(snprintf(path, sizeof(path), "%s/dev", root) > 0 && mkdir(path, 0755) == 0);
    CHECK(snprintf(path, sizeof(path), "%s/dev/null", root) > 0);
    /* This fixture only reads subprocess stdin; an empty file supplies EOF. */
    CHECK(tired_write_private_new(path, "", 0, &error) && chmod(path, 0644) == 0);
    CHECK(chmod(root, 0755) == 0);
    char *profiles[] = {"/tired", "profiles", "list", "--json", NULL};
    CHECK(child(root, false, false, profiles, 0));
    CHECK(snprintf(path, sizeof(path), "%s%s", root, TIRED_HELPER_CACHE) > 0);
    CHECK(access(path, F_OK) != 0 && errno == ENOENT);
    /* Only the downloaded frontend and OS runtime exist in this private root.
     * Automatic preparation must install its embedded helper without a manager. */
    CHECK(child(root, false, true, NULL, 0));
    CHECK(snprintf(cached, sizeof(cached), "%s/%s/tired-helper", TIRED_HELPER_CACHE,
                   tired_embedded_helper[0].sha256) > 0);
    CHECK(snprintf(path, sizeof(path), "%s%s", root, cached) > 0);
    struct stat before, after;
    CHECK(stat(path, &before) == 0 && before.st_uid == 0 && (before.st_mode & 07777) == 0755);
    CHECK(tired_read_file(path, 128U * 1024U * 1024U, &bytes, &error));
    CHECK(bytes.length == tired_embedded_helper[0].length &&
          memcmp(bytes.data, tired_embedded_helper[0].data, bytes.length) == 0);
    char *setup[] = {"/tired", "--internal-install-helper", NULL};
    CHECK(child(root, false, false, setup, 0));
    CHECK(stat(path, &after) == 0 && before.st_ino == after.st_ino);
    char *identify[] = {cached, "--build-id", NULL};
    CHECK(child(root, false, false, identify, 0));
    CHECK(child(root, true, true, NULL, 0));
    CHECK(child(root, true, false, setup, TIRED_AUTHORIZATION));
    CHECK(unlink(path) == 0);
    CHECK(child(root, true, true, NULL, 1));
    CHECK(access(path, F_OK) != 0 && errno == ENOENT);
    /* Reuse a matching package helper without filling the cache. An incompatible
     * installed executable remains untouched while the bundled helper is selected. */
    CHECK(tired_payload_path(true, &installed, &error));
    CHECK(copy_file(root, argv[2], installed.data));
    CHECK(child(root, false, true, NULL, 0));
    CHECK(access(path, F_OK) != 0 && errno == ENOENT);
    char package_path[4096];
    CHECK(snprintf(package_path, sizeof(package_path), "%s%s", root, installed.data) > 0);
    CHECK(unlink(package_path) == 0 && copy_file(root, "/bin/true", installed.data));
    CHECK(stat(package_path, &before) == 0);
    CHECK(child(root, false, true, NULL, 0));
    CHECK(stat(package_path, &after) == 0 && before.st_ino == after.st_ino);
    CHECK(access(path, F_OK) == 0);
    CHECK(snprintf(path, sizeof(path), "%s/var/lib/tired", root) > 0);
    CHECK(access(path, F_OK) != 0 && errno == ENOENT);
    result = 0;
cleanup:
    tired_text_destroy(&bytes);
    tired_text_destroy(&installed);
    if (created != NULL && nftw(created, remove_entry, 16, FTW_DEPTH | FTW_PHYS) != 0)
        result = 1;
    free(root);
    return result;
}
