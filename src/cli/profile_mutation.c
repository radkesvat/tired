#include "tired/file_retirement.h"
#include "tired/io.h"
#include "tired/layout.h"
#include "tired/mutation.h"
#include "tired/operation_lock.h"
#include "tired/profile_frontend.h"
#include "tired/publication.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static bool discard_retained(TiredDirectory *directory, const char *name,
                             const TiredFileFingerprint *expected, TiredError *error)
{
    TiredFileFingerprint actual = {0};
    if (!tired_file_fingerprint(directory, name, TIRED_PROFILE_LIMIT, &actual, error))
        return false;
    if (!tired_file_fingerprint_equal(&actual, expected))
        return tired_error_set(error, TIRED_CONFLICT, "profile-cleanup-changed",
                               "Profile database changed, but the retained file differs from its "
                               "expected state. Inspect it before cleanup.",
                               0);
    if (!tired_directory_check(directory, error))
        return false;
    if (unlinkat(tired_directory_fd(directory), name, 0) != 0)
        return tired_error_set(
            error, TIRED_RUNTIME_FAILED, "profile-cleanup-remove",
            "Profile database changed, but its retained file could not be removed.", errno);
    if (fsync(tired_directory_fd(directory)) != 0)
        return tired_error_set(
            error, TIRED_RUNTIME_FAILED, "profile-cleanup-sync",
            "Profile database changed, but cleanup durability could not be confirmed.", errno);
    return true;
}

bool tired_profiles_mutate(const TiredRequest *request, const char *bundled_directory,
                           TiredText *output, TiredError *error)
{
    bool user = tired_spec_choice_is(&request->overrides, TIRED_FIELD_SCOPE, "user");
    bool installing = strcmp(request->arguments.items[0].data, "install") == 0;
    if (request->arguments.count != 2 ||
        (!installing && strcmp(request->arguments.items[0].data, "remove") != 0))
        return tired_error_set(error, TIRED_INVALID, "profiles-mutation",
                               "Expected profiles install FILE or remove ID.", 0);
    if (!user && geteuid() != 0)
        return tired_error_set(error, TIRED_AUTHORIZATION, "profile-authority",
                               "Administrator profile changes require authorization. Use sudo "
                               "tired profiles install/remove, or --user for your local profiles.",
                               0);
    if (!request->yes)
        return tired_error_set(error, TIRED_INVALID, "profile-approval",
                               "Inspect/validate the profile, then repeat the explicit "
                               "install/remove action with --yes.",
                               0);
    TiredLayout layout = {0};
    TiredProfile profile = {0}, previous = {0};
    TiredProfileCatalog catalog = {0};
    TiredText contents = {0}, old = {0}, runtime = {0};
    TiredDirectory *directory = NULL, *lock_directory = NULL;
    TiredOperationLock *lock = NULL;
    TiredPublication *publication = NULL;
    TiredFileFingerprint fingerprint = {0};
    TiredFileRetirement retired = {0};
    bool ok = false;
    if (!tired_layout_discover_profiles(user, &layout, error))
        goto done;
    const char *id = request->arguments.items[1].data;
    if (installing)
    {
        if (!tired_read_file(id, TIRED_PROFILE_LIMIT, &contents, error) ||
            !tired_profile_parse(contents.data, contents.length, &profile, error))
            goto done;
        id = profile.id;
    }
    size_t length = strlen(id);
    if (length == 0 || length > 80 || strchr(id, '/') != NULL || strstr(id, "..") != NULL)
    {
        tired_error_set(error, TIRED_INVALID, "profile-id",
                        "Profile ID is not a safe local profile name.", 0);
        goto done;
    }
    for (size_t i = 0; i < length; ++i)
        if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= '0' && id[i] <= '9') || id[i] == '-' ||
              id[i] == '_'))
        {
            tired_error_set(error, TIRED_INVALID, "profile-id",
                            "Profile ID uses unsupported characters.", 0);
            goto done;
        }
    const TiredText *lock_path =
        user ? &layout.paths[TIRED_PATH_CONFIG] : &layout.paths[TIRED_PATH_OPERATION_LOCK];
    const char *slash = strrchr(lock_path->data, '/');
    if (slash == NULL)
    {
        tired_error_set(error, TIRED_INVALID, "profile-lock-path",
                        "Profile lock directory must be absolute.", 0);
        goto done;
    }
    if (!tired_text_set(&runtime, lock_path->data, (size_t)(slash - lock_path->data), 4096,
                        error) ||
        !tired_directory_ensure(runtime.data, true, &lock_directory, error) ||
        !tired_operation_lock_acquire(lock_directory, &lock, error) ||
        !tired_directory_ensure(layout.paths[TIRED_PATH_PROFILES].data, false, &directory, error))
        goto done;
    char filename[96];
    (void)snprintf(filename, sizeof(filename), "%s.json", id);
    if (!tired_file_snapshot(directory, filename, TIRED_PROFILE_LIMIT, &fingerprint, &old, error))
        goto done;
    if (fingerprint.exists)
    {
        if (fingerprint.uid != geteuid() || (fingerprint.mode & 0022) != 0)
        {
            tired_error_set(error, TIRED_AUTHORIZATION, "profile-trust",
                            "Local profile must be owned by the invoking user and must not be "
                            "group- or other-writable.",
                            0);
            goto done;
        }
        if (!tired_profile_parse(old.data, old.length, &previous, error))
            goto done;
    }
    if (installing)
    {
        if (!tired_profiles_discover(bundled_directory, user, &catalog, error))
            goto done;
        bool replaces_existing = fingerprint.exists;
        for (size_t i = 0; i < catalog.count; ++i)
            replaces_existing |= strcmp(catalog.items[i].profile.id, id) == 0;
        struct json_object *replaces = NULL;
        if (replaces_existing &&
            (!json_object_object_get_ex(profile.document, "replaces", &replaces) ||
             !json_object_is_type(replaces, json_type_string) ||
             strcmp(json_object_get_string(replaces), id) != 0))
        {
            tired_error_set(error, TIRED_CONFLICT, "profile-replacement",
                            "Replacing an existing/bundled profile requires matching explicit "
                            "replaces metadata.",
                            0);
            goto done;
        }
        if (!tired_publication_prepare(directory, filename, contents.data, contents.length,
                                       user ? 0600 : 0644, &publication, error))
            goto done;
        ok = fingerprint.exists ? tired_publication_replace(publication, lock, &fingerprint, error)
                                : tired_publication_commit(publication, lock, error);
        if (ok && fingerprint.exists)
        {
            const char *retained = tired_publication_temporary_name(publication);
            ok = discard_retained(directory, retained, &fingerprint, error);
        }
    }
    else
    {
        if (!fingerprint.exists)
        {
            tired_error_set(error, TIRED_NOT_FOUND, "local-profile-not-found",
                            "No local override exists; bundled profiles are package-owned and "
                            "cannot be removed here.",
                            0);
            goto done;
        }
        char uuid[37];
        if (!tired_uuid_create(uuid, error))
            goto done;
        ok = tired_file_retire(directory, filename, uuid, &fingerprint, lock, &retired, error);
        if (ok)
        {
            char retained[64];
            (void)snprintf(retained, sizeof(retained), ".tired-%s.removed", uuid);
            ok = discard_retained(directory, retained, &fingerprint, error);
        }
    }
    if (ok)
    {
        const char *message =
            request->json
                ? "{\"schema_version\":1,\"command\":\"profiles\",\"ok\":true,\"exit_code\":0,"
                  "\"existing_services_changed\":false}\n"
                : "Updated the local profile database. Existing service snapshots are unchanged.\n";
        ok = tired_text_set(output, message, strlen(message), TIRED_INPUT_LIMIT, error);
    }
done:
    if (!ok && publication != NULL && !tired_publication_published(publication))
    {
        TiredError cleanup = {0};
        if (!tired_publication_discard(publication, &cleanup))
            *error = cleanup;
    }
    tired_publication_destroy(publication);
    tired_operation_lock_destroy(lock);
    tired_directory_destroy(lock_directory);
    tired_directory_destroy(directory);
    tired_profile_destroy(&profile);
    tired_profile_destroy(&previous);
    tired_catalog_destroy(&catalog);
    tired_layout_destroy(&layout);
    tired_text_destroy(&contents);
    tired_text_destroy(&old);
    tired_text_destroy(&runtime);
    return ok;
}
