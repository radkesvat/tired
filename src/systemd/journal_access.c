#include "tired/journal_access.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <systemd/sd-journal.h>
#include <unistd.h>

typedef struct
{
    TiredJournalAccess report;
    sd_id128_t machine;
    bool user;
    char user_prefix[32];
} Scan;
static void issue(Scan *scan, int number, const char *code, const char *message)
{
    ++scan->report.issues;
    scan->report.complete = false;
    if (scan->report.first_issue.code != NULL)
        return;
    TiredStatus status = number == EACCES || number == EPERM                 ? TIRED_AUTHORIZATION
                         : number == EPROTONOSUPPORT || number == EOPNOTSUPP ? TIRED_UNSUPPORTED
                                                                             : TIRED_RUNTIME_FAILED;
    tired_error_set(&scan->report.first_issue, status, code, message, number);
}
static bool suffix(const char *name, const char *end)
{
    size_t a = strlen(name), b = strlen(end);
    return a >= b && memcmp(name + a - b, end, b) == 0;
}
static bool prefix(const char *name, const char *type)
{
    size_t length = strlen(type);
    return strncmp(name, type, length) == 0 &&
           (name[length] == '@' || strcmp(name + length, ".journal") == 0 ||
            strcmp(name + length, ".journal~") == 0);
}
static bool wanted(Scan *scan, const char *name)
{
    return (suffix(name, ".journal") || suffix(name, ".journal~")) &&
           (!scan->user || prefix(name, "system") || prefix(name, scan->user_prefix));
}
static void file_check(Scan *scan, int parent, const char *name)
{
    ++scan->report.files_seen;
    int fd = openat(parent, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
    {
        issue(scan, errno, "journal-file-access", "Cannot open a selected journal file.");
        return;
    }
    struct stat st;
    if (fstat(fd, &st) < 0)
        issue(scan, errno, "journal-file-stat", "Cannot inspect a selected journal file.");
    else if (!S_ISREG(st.st_mode))
        issue(scan, EINVAL, "journal-file-type", "A selected journal path is not a regular file.");
    else
    {
        sd_journal *journal = NULL;
        /* systemd 249's open_files_fd passes NULL into path hashing. Use a
         * process-owned pinned descriptor path with its working path API. */
        char descriptor[64];
        (void)snprintf(descriptor, sizeof(descriptor), "/proc/self/fd/%d", fd);
        const char *paths[] = {descriptor, NULL};
        int rc = sd_journal_open_files(&journal, paths, 0);
        if (rc >= 0)
        {
            ++scan->report.files_opened;
            sd_journal_close(journal);
            close(fd);
            return;
        }
        issue(scan, -rc, "journal-file-format", "Native journal opening rejected a selected file.");
    }
    close(fd);
}
static void directory_check(Scan *scan, int fd, bool root, bool runtime)
{
    DIR *directory = fdopendir(fd);
    if (directory == NULL)
    {
        issue(scan, errno, "journal-directory-read", "Cannot enumerate a journal directory.");
        close(fd);
        return;
    }
    ++scan->report.directories_opened;
    for (;;)
    {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (entry == NULL)
        {
            if (errno != 0)
                issue(scan, errno, "journal-directory-read",
                      "Cannot enumerate a journal directory.");
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (scan->report.entries_examined == TIRED_JOURNAL_SCAN_LIMIT)
        {
            issue(scan, E2BIG, "journal-access-limit",
                  "Journal access inventory exceeds its entry limit.");
            break;
        }
        ++scan->report.entries_examined;
        if (wanted(scan, entry->d_name))
            file_check(scan, fd, entry->d_name);
        else if (root && strlen(entry->d_name) == 32)
        {
            sd_id128_t machine;
            if (sd_id128_from_string(entry->d_name, &machine) < 0 ||
                (!runtime && !sd_id128_equal(machine, scan->machine)))
                continue;
            int child = openat(fd, entry->d_name, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
            if (child < 0)
                issue(scan, errno, "journal-directory-access",
                      "Cannot open a journal machine directory.");
            else
                directory_check(scan, child, false, runtime);
        }
        if (scan->report.entries_examined == TIRED_JOURNAL_SCAN_LIMIT && !scan->report.complete)
            break;
    }
    closedir(directory);
}
bool tired_journal_access_check(const char *const roots[2], const char *machine_id, bool user_scope,
                                uid_t uid, TiredJournalAccess *output, TiredError *error)
{
    assert(machine_id != NULL && output != NULL);
    const char *defaults[] = {"/run/log/journal", "/var/log/journal"};
    if (roots == NULL)
        roots = defaults;
    assert(roots[0] != NULL && roots[1] != NULL);
    Scan scan = {.report.complete = true, .user = user_scope};
    if (strlen(machine_id) != 32 || sd_id128_from_string(machine_id, &scan.machine) < 0 ||
        (user_scope && uid == (uid_t)-1))
        return tired_error_set(error, TIRED_INVALID, "journal-access-scope",
                               "Invalid journal inventory identity.", 0);
    (void)snprintf(scan.user_prefix, sizeof(scan.user_prefix), "user-%" PRIuMAX, (uintmax_t)uid);
    for (size_t i = 0; i < 2; ++i)
    {
        int fd = open(roots[i], O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
        if (fd < 0)
        {
            if (errno == ENOENT)
                ++scan.report.missing_roots;
            else
                issue(&scan, errno, "journal-root-access", "Cannot open a journal storage root.");
        }
        else
            directory_check(&scan, fd, true, i == 0);
    }
    *output = scan.report;
    tired_error_clear(error);
    return true;
}
