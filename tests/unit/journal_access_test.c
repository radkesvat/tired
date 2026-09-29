#include "tired/journal_access.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
static bool empty(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0)
        return false;
    return close(fd) == 0;
}
int main(void)
{
    int result = 1, saved = -1;
    char fixture[] = "journal-access-XXXXXX";
    char *directory = NULL;
    bool entered = false;
    const char *machine = "0123456789abcdef0123456789abcdef";
    const char *foreign = "1123456789abcdef0123456789abcdef";
    const char *roots[] = {"runtime", "persistent"};
    TiredJournalAccess access = {0};
    TiredError error = {0};
    char path[128];
    size_t created = 0;
    saved = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(saved >= 0);
    directory = mkdtemp(fixture);
    CHECK(directory != NULL && chdir(directory) == 0);
    entered = true;
    CHECK(tired_journal_access_check(roots, machine, false, 0, &access, &error));
    CHECK(access.complete && access.missing_roots == 2 && access.files_seen == 0);
    CHECK(mkdir("runtime", 0700) == 0 && mkdir("persistent", 0700) == 0);
    CHECK(tired_journal_access_check(roots, machine, false, 0, &access, &error));
    CHECK(access.complete && access.directories_opened == 2 && access.missing_roots == 0);
    CHECK(empty("runtime/user-2000.journal"));
    CHECK(tired_journal_access_check(roots, machine, true, 1000, &access, &error));
    CHECK(access.complete && access.files_seen == 0);
    CHECK(empty("runtime/user-1000.journal"));
    CHECK(tired_journal_access_check(roots, machine, true, 1000, &access, &error));
    CHECK(!access.complete && access.files_seen == 1 && access.files_opened == 0 &&
          access.issues == 1);
    CHECK(strcmp(access.first_issue.code, "journal-file-format") == 0);
    CHECK(tired_journal_access_check(roots, machine, false, 0, &access, &error));
    CHECK(!access.complete && access.files_seen == 2);
    CHECK(unlink("runtime/user-1000.journal") == 0 && unlink("runtime/user-2000.journal") == 0);
    CHECK(symlink("missing", "runtime/system.journal") == 0);
    CHECK(tired_journal_access_check(roots, machine, true, 1000, &access, &error));
    CHECK(!access.complete && strcmp(access.first_issue.code, "journal-file-access") == 0);
    CHECK(unlink("runtime/system.journal") == 0);
    CHECK(mkfifo("runtime/system.journal", 0600) == 0);
    CHECK(tired_journal_access_check(roots, machine, true, 1000, &access, &error));
    CHECK(!access.complete && strcmp(access.first_issue.code, "journal-file-type") == 0);
    CHECK(unlink("runtime/system.journal") == 0);
    (void)snprintf(path, sizeof(path), "persistent/%s", foreign);
    CHECK(mkdir(path, 0700) == 0);
    (void)snprintf(path, sizeof(path), "persistent/%s/system.journal", foreign);
    CHECK(empty(path));
    CHECK(tired_journal_access_check(roots, machine, false, 0, &access, &error));
    CHECK(access.complete && access.files_seen == 0);
    CHECK(unlink(path) == 0);
    (void)snprintf(path, sizeof(path), "persistent/%s", foreign);
    CHECK(rmdir(path) == 0);
    (void)snprintf(path, sizeof(path), "runtime/%s", foreign);
    CHECK(mkdir(path, 0700) == 0);
    (void)snprintf(path, sizeof(path), "runtime/%s/system@archive.journal~", foreign);
    CHECK(empty(path));
    CHECK(tired_journal_access_check(roots, machine, true, 1000, &access, &error));
    CHECK(!access.complete && access.files_seen == 1 && access.directories_opened == 3);
    CHECK(unlink(path) == 0);
    (void)snprintf(path, sizeof(path), "runtime/%s", foreign);
    CHECK(rmdir(path) == 0);
    if (getuid() == 0)
    {
        pid_t child = fork();
        CHECK(child >= 0);
        if (child == 0)
        {
            if (setgid(65534) != 0 || setuid(65534) != 0)
                _exit(2);
            bool ok = tired_journal_access_check(roots, machine, true, 65534, &access, &error);
            _exit(ok && !access.complete && access.first_issue.status == TIRED_AUTHORIZATION ? 0
                                                                                             : 3);
        }
        int status;
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    for (; created <= TIRED_JOURNAL_SCAN_LIMIT; ++created)
    {
        (void)snprintf(path, sizeof(path), "runtime/ignored-%zu", created);
        CHECK(empty(path));
    }
    CHECK(tired_journal_access_check(roots, machine, false, 0, &access, &error));
    CHECK(!access.complete && access.entries_examined == TIRED_JOURNAL_SCAN_LIMIT);
    CHECK(strcmp(access.first_issue.code, "journal-access-limit") == 0);
    TiredJournalAccess previous = access;
    CHECK(!tired_journal_access_check(roots, "invalid", false, 0, &access, &error));
    CHECK(memcmp(&access, &previous, sizeof(access)) == 0);
    result = 0;
cleanup:
    if (entered)
    {
        for (size_t i = 0; i < created; ++i)
        {
            (void)snprintf(path, sizeof(path), "runtime/ignored-%zu", i);
            unlink(path);
        }
        unlink("runtime/system.journal");
        unlink("runtime/user-1000.journal");
        unlink("runtime/user-2000.journal");
        (void)snprintf(path, sizeof(path), "runtime/%s/system@archive.journal~", foreign);
        unlink(path);
        (void)snprintf(path, sizeof(path), "runtime/%s", foreign);
        rmdir(path);
        (void)snprintf(path, sizeof(path), "persistent/%s/system.journal", foreign);
        unlink(path);
        (void)snprintf(path, sizeof(path), "persistent/%s", foreign);
        rmdir(path);
        rmdir("runtime");
        rmdir("persistent");
        if (saved >= 0)
        {
            (void)fchdir(saved);
            rmdir(directory);
        }
    }
    else if (directory != NULL)
        rmdir(directory);
    if (saved >= 0)
        close(saved);
    return result;
}
