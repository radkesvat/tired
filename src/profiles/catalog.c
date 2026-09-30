#include "tired/catalog.h"
#include "tired/io.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void entry_destroy(TiredProfileEntry *entry)
{
    tired_profile_destroy(&entry->profile);
    tired_text_destroy(&entry->path);
    *entry = (TiredProfileEntry){0};
}
void tired_catalog_destroy(TiredProfileCatalog *catalog)
{
    if (catalog == NULL)
        return;
    for (size_t i = 0; i < catalog->count; ++i)
        entry_destroy(&catalog->items[i]);
    free(catalog->items);
    *catalog = (TiredProfileCatalog){0};
}
static bool trusted(int fd, uid_t owner, bool directory, TiredError *error)
{
    struct stat status;
    if (fstat(fd, &status) != 0)
        return tired_error_set(error, TIRED_INVALID, "profile-stat", "Cannot inspect profile path.",
                               errno);
    if ((directory ? !S_ISDIR(status.st_mode) : !S_ISREG(status.st_mode)) ||
        (status.st_uid != 0 && status.st_uid != owner) || (status.st_mode & 0022) != 0 ||
        (!directory && status.st_nlink != 1))
        return tired_error_set(
            error, TIRED_CONFLICT, "profile-trust",
            "Profile path has unsafe type, ownership, permissions, or hard links.", 0);
    return true;
}
static int open_directory(const char *path, uid_t owner, bool optional, TiredError *error)
{
    if (path[0] != '/' || strnlen(path, TIRED_INPUT_LIMIT + 1) > TIRED_INPUT_LIMIT)
    {
        tired_error_set(error, TIRED_INVALID, "profile-path",
                        "Profile directory must be a bounded absolute path.", 0);
        return -1;
    }
    int fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
    {
        tired_error_set(error, TIRED_INVALID, "profile-open", "Cannot open filesystem root.",
                        errno);
        return -1;
    }
    if (!trusted(fd, owner, true, error))
    {
        (void)close(fd);
        return -1;
    }
    const char *part = path + 1;
    while (*part != '\0')
    {
        const char *end = strchr(part, '/');
        size_t length = end == NULL ? strlen(part) : (size_t)(end - part);
        if (length == 0 || length > 255 || (length == 1 && part[0] == '.') ||
            (length == 2 && part[0] == '.' && part[1] == '.'))
        {
            (void)close(fd);
            tired_error_set(error, TIRED_INVALID, "profile-path",
                            "Profile directory must use normalized path components.", 0);
            return -1;
        }
        char component[256];
        memcpy(component, part, length);
        component[length] = '\0';
        int next = openat(fd, component, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        int saved_errno = errno;
        (void)close(fd);
        if (next < 0)
        {
            if (optional && saved_errno == ENOENT)
            {
                tired_error_clear(error);
                return -2;
            }
            tired_error_set(error, TIRED_INVALID, "profile-open",
                            "Cannot open a trusted profile directory component.", saved_errno);
            return -1;
        }
        fd = next;
        if (!trusted(fd, owner, true, error))
        {
            (void)close(fd);
            return -1;
        }
        if (end == NULL)
            break;
        part = end + 1;
    }
    return fd;
}
static int compare(const void *a, const void *b)
{
    return strcmp(((const TiredText *)a)->data, ((const TiredText *)b)->data);
}
static bool digest(const TiredText *contents, char output[65], TiredError *error)
{
    unsigned char bytes[EVP_MAX_MD_SIZE];
    unsigned length = 0;
    if (EVP_Digest(contents->data, contents->length, bytes, &length, EVP_sha256(), NULL) != 1 ||
        length != 32)
        return tired_error_set(error, TIRED_INTERNAL, "profile-digest",
                               "Cannot compute profile content digest.", 0);
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i)
    {
        output[i * 2] = hex[bytes[i] >> 4];
        output[i * 2 + 1] = hex[bytes[i] & 15];
    }
    output[64] = '\0';
    return true;
}

bool tired_catalog_add_directory(TiredProfileCatalog *catalog, const char *path,
                                 TiredProfileOrigin origin, uid_t trusted_owner, bool optional,
                                 TiredError *error)
{
    assert(catalog != NULL && path != NULL);
    int fd = open_directory(path, trusted_owner, optional, error);
    if (fd == -2)
        return true;
    if (fd < 0)
        return false;
    DIR *directory = fdopendir(fd);
    if (directory == NULL)
    {
        int saved_errno = errno;
        (void)close(fd);
        return tired_error_set(error, TIRED_INVALID, "profile-directory",
                               "Cannot enumerate profile directory.", saved_errno);
    }
    TiredTextList names = {0};
    TiredProfileEntry item = {0};
    TiredText contents = {0};
    size_t original_count = catalog->count, visited = 0;
    struct dirent *entry;
    for (;;)
    {
        errno = 0;
        entry = readdir(directory);
        if (entry == NULL)
        {
            if (errno != 0)
            {
                tired_error_set(error, TIRED_INVALID, "profile-directory",
                                "Cannot read profile directory entries.", errno);
                goto fail;
            }
            break;
        }
        if (++visited > 4096)
        {
            tired_error_set(error, TIRED_INVALID, "profile-count",
                            "Profile directory contains too many entries.", 0);
            goto fail;
        }
        size_t length = strlen(entry->d_name);
        if (length > 5 && strcmp(entry->d_name + length - 5, ".json") == 0 &&
            !tired_text_list_append(&names, entry->d_name, length, 256, 65536, error))
            goto fail;
    }
    if (names.count != 0)
        qsort(names.items, names.count, sizeof(*names.items), compare);
    for (size_t i = 0; i < names.count; ++i)
    {
        if (catalog->count >= 256)
        {
            tired_error_set(error, TIRED_INVALID, "profile-count",
                            "Profile catalog exceeds 256 entries.", 0);
            goto fail;
        }
        int file = openat(fd, names.items[i].data, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
        if (file < 0)
        {
            tired_error_set(error, TIRED_INVALID, "profile-open",
                            "Cannot open a profile without following symlinks.", errno);
            goto fail;
        }
        bool ok = trusted(file, trusted_owner, false, error) &&
                  tired_read_fd(file, TIRED_PROFILE_LIMIT, &contents, error);
        if (close(file) != 0 && ok)
            ok = tired_error_set(error, TIRED_INVALID, "profile-close",
                                 "Cannot close profile input.", errno);
        if (!ok || !tired_profile_parse(contents.data, contents.length, &item.profile, error) ||
            !digest(&contents, item.digest, error))
            goto fail;
        for (size_t j = 0; j < catalog->count; ++j)
            if (strcmp(catalog->items[j].profile.id, item.profile.id) == 0)
            {
                const TiredProfileEntry *existing = &catalog->items[j];
                const TiredProfile *replacement =
                    origin > existing->origin ? &item.profile : &existing->profile;
                struct json_object *replaces = NULL;
                if (origin == existing->origin ||
                    !json_object_object_get_ex(replacement->document, "replaces", &replaces) ||
                    strcmp(json_object_get_string(replaces), item.profile.id) != 0)
                {
                    tired_error_set(
                        error, TIRED_CONFLICT, "profile-duplicate",
                        "Duplicate profile IDs require matching replaces metadata in the local "
                        "replacement; duplicates within one origin are not allowed.",
                        0);
                    goto fail;
                }
            }
        TiredText base = {.data = (char *)path, .length = strlen(path)};
        if (!tired_path_absolute(&base, names.items[i].data, names.items[i].length, &item.path,
                                 error))
            goto fail;
        item.origin = origin;
        item.trusted_owner = trusted_owner;
        TiredProfileEntry *grown = realloc(catalog->items, (catalog->count + 1) * sizeof(*grown));
        if (grown == NULL)
        {
            tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot grow profile catalog.",
                            errno);
            goto fail;
        }
        catalog->items = grown;
        catalog->items[catalog->count++] = item;
        item = (TiredProfileEntry){0};
    }
    if (closedir(directory) != 0)
    {
        directory = NULL;
        tired_error_set(error, TIRED_INVALID, "profile-close", "Cannot close profile directory.",
                        errno);
        goto fail;
    }
    tired_text_list_destroy(&names);
    tired_text_destroy(&contents);
    tired_error_clear(error);
    return true;
fail:
    if (directory != NULL)
        (void)closedir(directory);
    entry_destroy(&item);
    tired_text_list_destroy(&names);
    tired_text_destroy(&contents);
    while (catalog->count > original_count)
        entry_destroy(&catalog->items[--catalog->count]);
    return false;
}

bool tired_catalog_entry_active(const TiredProfileCatalog *catalog, size_t index, bool user_scope)
{
    assert(catalog != NULL && index < catalog->count);
    const TiredProfileEntry *candidate = &catalog->items[index];
    if (!user_scope && candidate->origin == TIRED_PROFILE_USER)
        return false;
    for (size_t i = 0; i < catalog->count; ++i)
        if ((user_scope || catalog->items[i].origin != TIRED_PROFILE_USER) &&
            catalog->items[i].origin > candidate->origin &&
            strcmp(catalog->items[i].profile.id, candidate->profile.id) == 0)
            return false;
    return true;
}

bool tired_catalog_select(const TiredProfileCatalog *catalog, const TiredText *executable,
                          const char *selection, bool user_scope, const TiredProfileEntry **entry,
                          size_t *match_count, TiredError *error)
{
    assert(catalog != NULL && executable != NULL && entry != NULL && match_count != NULL);
    bool automatic = selection == NULL || strcmp(selection, "auto") == 0;
    const TiredProfileEntry *selected = NULL, *generic = NULL;
    size_t matches = 0;
    if (selection != NULL && strcmp(selection, "none") == 0)
    {
        *entry = NULL;
        *match_count = 0;
        tired_error_clear(error);
        return true;
    }
    for (size_t i = 0; i < catalog->count; ++i)
    {
        const TiredProfileEntry *candidate = &catalog->items[i];
        if (!tired_catalog_entry_active(catalog, i, user_scope))
            continue;
        if (strcmp(candidate->profile.id, "generic") == 0)
            generic = candidate;
        bool match = automatic ? strcmp(candidate->profile.id, "generic") != 0 &&
                                     tired_profile_matches(&candidate->profile, executable)
                               : strcmp(candidate->profile.id, selection) == 0;
        if (match)
        {
            selected = candidate;
            ++matches;
        }
    }
    if (!automatic && matches == 0)
        return tired_error_set(error, TIRED_NOT_FOUND, "profile-not-found",
                               "Requested profile is not available in this scope.", 0);
    if (matches > 1)
        selected = NULL;
    else if (automatic && matches == 0)
        selected = generic;
    *entry = selected;
    *match_count = matches;
    tired_error_clear(error);
    return true;
}
