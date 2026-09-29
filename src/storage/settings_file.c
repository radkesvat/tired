#include "tired/capture.h"
#include "tired/io.h"
#include "tired/settings.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool trusted(int fd, uid_t owner, bool directory, TiredError *error)
{
    struct stat status;
    if (fstat(fd, &status) != 0)
        return tired_error_set(error, TIRED_INVALID, "settings-stat",
                               "Cannot inspect settings path.", errno);
    if ((directory ? !S_ISDIR(status.st_mode) : !S_ISREG(status.st_mode)) ||
        (status.st_uid != 0 && status.st_uid != owner) || (status.st_mode & 0022) != 0 ||
        (!directory && status.st_nlink != 1))
        return tired_error_set(
            error, TIRED_CONFLICT, "settings-file-trust",
            "Settings path has unsafe type, ownership, permissions, or hard links.", 0);
    return true;
}

static bool load_layer(const char *path, uid_t owner, TiredSettingsOrigin origin,
                       TiredSettings *settings, TiredError *error)
{
    if (path == NULL)
        return true;
    size_t size = strnlen(path, TIRED_INPUT_LIMIT + 1);
    if (size < 2 || size > TIRED_INPUT_LIMIT || path[0] != '/' || path[size - 1] == '/' ||
        !tired_validate_text(path, size, true, error))
        return tired_error_set(error, TIRED_INVALID, "settings-path",
                               "Settings path must be a bounded normalized absolute file path.", 0);
    /* Validate the complete path before treating any missing component as absence. */
    for (const char *part = path + 1; *part != '\0';)
    {
        const char *end = strchr(part, '/');
        size_t length = end == NULL ? strlen(part) : (size_t)(end - part);
        if (length == 0 || length > 255 || (length == 1 && part[0] == '.') ||
            (length == 2 && part[0] == '.' && part[1] == '.'))
            return tired_error_set(error, TIRED_INVALID, "settings-path",
                                   "Settings path components must be normalized.", 0);
        part += length;
        if (*part != '\0')
            ++part;
    }
    int fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return tired_error_set(error, TIRED_INVALID, "settings-open",
                               "Cannot open filesystem root for settings.", errno);
    TiredText contents = {0};
    TiredSettings layer = {0};
    bool ok = trusted(fd, owner, true, error);
    for (const char *part = path + 1; ok && *part != '\0';)
    {
        const char *end = strchr(part, '/');
        size_t length = end == NULL ? strlen(part) : (size_t)(end - part);
        char component[256];
        memcpy(component, part, length);
        component[length] = '\0';
        bool directory = end != NULL;
        int next;
        do
        {
            next = openat(fd, component,
                          O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY |
                              (directory ? O_DIRECTORY : 0));
        } while (next < 0 && errno == EINTR);
        int saved_errno = errno;
        (void)close(fd);
        fd = next;
        if (next < 0)
        {
            if (saved_errno == ENOENT)
            {
                tired_error_clear(error);
                goto done;
            }
            ok = tired_error_set(error, TIRED_INVALID, "settings-open",
                                 "Cannot open a trusted settings path component.", saved_errno);
            break;
        }
        ok = trusted(fd, owner, directory, error);
        if (!directory)
        {
            if (ok)
                ok = tired_read_fd(fd, TIRED_INPUT_LIMIT, &contents, error) &&
                     trusted(fd, owner, false, error) &&
                     tired_settings_parse(contents.data, contents.length, &layer, error);
            break;
        }
        part = end + 1;
    }
    if (fd >= 0)
    {
        if (close(fd) != 0 && ok)
            ok = tired_error_set(error, TIRED_INVALID, "settings-close",
                                 "Cannot close settings file.", errno);
        fd = -1;
    }
    if (ok)
        ok = tired_settings_merge(settings, &layer, origin, error);
done:
    if (fd >= 0)
        (void)close(fd);
    tired_text_destroy(&contents);
    tired_settings_destroy(&layer);
    return ok;
}

bool tired_settings_load(const char *administrator_path, const char *user_path, uid_t user,
                         TiredSettings *settings, TiredError *error)
{
    assert(settings != NULL);
    TiredSettings loaded = {0};
    tired_settings_defaults(&loaded);
    if (!load_layer(administrator_path, 0, TIRED_SETTINGS_ADMIN, &loaded, error) ||
        !load_layer(user_path, user, TIRED_SETTINGS_USER, &loaded, error))
    {
        tired_settings_destroy(&loaded);
        return false;
    }
    tired_settings_destroy(settings);
    *settings = loaded;
    tired_error_clear(error);
    return true;
}
