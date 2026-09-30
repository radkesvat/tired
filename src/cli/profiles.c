#include "tired/config_frontend.h"
#include "tired/encode.h"
#include "tired/io.h"
#include "tired/plan_output.h"
#include "tired/profile_frontend.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool tired_profiles_discover(const char *bundled_directory, bool user_scope,
                             TiredProfileCatalog *catalog, TiredError *error)
{
    return tired_profiles_discover_settings(bundled_directory, user_scope, NULL, catalog, error);
}

bool tired_profiles_discover_settings(const char *bundled_directory, bool user_scope,
                                      const TiredSettings *settings, TiredProfileCatalog *catalog,
                                      TiredError *error)
{
    TiredProfileCatalog loaded = {0};
    TiredAccount account = {0};
    TiredText directory = {0};
    TiredBuffer path;
    tired_buffer_init(&path, TIRED_INPUT_LIMIT);
    if (!tired_catalog_add_directory(&loaded, bundled_directory, TIRED_PROFILE_BUNDLED, getuid(),
                                     false, error) ||
        !tired_catalog_add_directory(&loaded, "/etc/tired/profiles.d", TIRED_PROFILE_ADMIN, 0, true,
                                     error))
        goto fail;
    if (settings != NULL && settings->profile_directories.count != 0)
    {
        if (settings->origins[TIRED_SETTING_PROFILE_DIRECTORIES] != TIRED_SETTINGS_ADMIN)
        {
            tired_error_set(error, TIRED_INVALID, "profile-directory-authority",
                            "Additional profile directories require administrator settings.", 0);
            goto fail;
        }
        for (size_t i = 0; i < settings->profile_directories.count; ++i)
            if (!tired_catalog_add_directory(&loaded, settings->profile_directories.items[i].data,
                                             TIRED_PROFILE_ADMIN, 0, false, error))
                goto fail;
    }
    if (user_scope)
    {
        if (!tired_account_by_uid(getuid(), &account, error))
            goto fail;
        const char *xdg = getenv("XDG_CONFIG_HOME");
        if (xdg != NULL && xdg[0] != '\0')
        {
            size_t size = strnlen(xdg, TIRED_INPUT_LIMIT + 1);
            if (size > TIRED_INPUT_LIMIT || xdg[0] != '/')
            {
                tired_error_set(error, TIRED_INVALID, "xdg-path",
                                "User configuration directory must be absolute.", 0);
                goto fail;
            }
            while (size > 1 && xdg[size - 1] == '/')
                --size;
            if (size > 1 && !tired_buffer_append(&path, xdg, size, error))
                goto fail;
        }
        else
        {
            if (account.home.length == 0 || account.home.data[0] != '/')
            {
                tired_error_set(error, TIRED_INVALID, "profile-home",
                                "Account home directory must be absolute for profile discovery.",
                                0);
                goto fail;
            }
            size_t size = account.home.length;
            while (size > 0 && account.home.data[size - 1] == '/')
                --size;
            if (!tired_buffer_append(&path, account.home.data, size, error) ||
                !tired_buffer_append(&path, "/.config", 8, error))
                goto fail;
        }
        if (!tired_buffer_append(&path, "/tired/profiles.d", sizeof("/tired/profiles.d") - 1,
                                 error) ||
            !tired_buffer_take(&path, &directory, error) ||
            !tired_catalog_add_directory(&loaded, directory.data, TIRED_PROFILE_USER, getuid(),
                                         true, error))
            goto fail;
    }
    tired_account_destroy(&account);
    tired_text_destroy(&directory);
    tired_buffer_destroy(&path);
    tired_catalog_destroy(catalog);
    *catalog = loaded;
    tired_error_clear(error);
    return true;
fail:
    tired_account_destroy(&account);
    tired_text_destroy(&directory);
    tired_buffer_destroy(&path);
    tired_catalog_destroy(&loaded);
    return false;
}

static bool add(struct json_object *object, const char *key, struct json_object *child)
{
    if (child == NULL)
        return false;
    if (json_object_object_add(object, key, child) != 0)
    {
        json_object_put(child);
        return false;
    }
    return true;
}
static bool json_output(struct json_object *object, TiredText *output, TiredError *error)
{
    const char *json = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PRETTY);
    if (json == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation",
                               "Cannot serialize profile output.", 0);
    TiredBuffer buffer;
    tired_buffer_init(&buffer, 4U * TIRED_INPUT_LIMIT);
    /* Encode raw C1 characters as JSON Unicode escapes. */
    static const char hex[] = "0123456789abcdef";
    bool ok = true;
    for (size_t i = 0; ok && json[i] != '\0'; ++i)
    {
        if ((unsigned char)json[i] == 0xc2 && (unsigned char)json[i + 1] >= 0x80 &&
            (unsigned char)json[i + 1] <= 0x9f)
        {
            unsigned char c = (unsigned char)json[++i];
            char escaped[] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
            ok = tired_buffer_append(&buffer, escaped, sizeof(escaped), error);
        }
        else
            ok = tired_buffer_append(&buffer, json + i, 1, error);
    }
    if (ok)
        ok = tired_buffer_append(&buffer, "\n", 1, error) &&
             tired_buffer_take(&buffer, output, error);
    tired_buffer_destroy(&buffer);
    return ok;
}

static bool explain(const TiredRequest *request, const char *bundled_directory, TiredText *output,
                    TiredError *error)
{
    TiredPlan plan = {0};
    TiredSettings settings = {0};
    TiredProfileCatalog catalog = {0};
    TiredProfileContext context = {.systemd_version = 249};
    bool user = tired_spec_choice_is(&request->overrides, TIRED_FIELD_SCOPE, "user");
    bool ok = tired_config_discover(request, &settings, error) &&
              tired_plan_prepare_settings(request, &settings, &plan, error);
    if (ok && (request->profile.data == NULL || strcmp(request->profile.data, "none") != 0))
        ok =
            tired_profiles_discover_settings(bundled_directory, user, &settings, &catalog, error) &&
            tired_plan_apply_profiles(&plan, &catalog, request->profile.data, &context, error);
    if (ok)
        ok = tired_plan_explain_output(&plan, request->json, output, error);
    tired_plan_destroy(&plan);
    tired_settings_destroy(&settings);
    tired_catalog_destroy(&catalog);
    return ok;
}

bool tired_profiles_command(const TiredRequest *request, const char *bundled_directory,
                            TiredText *output, TiredError *error)
{
    if (request->arguments.count > 0 && (strcmp(request->arguments.items[0].data, "install") == 0 ||
                                         strcmp(request->arguments.items[0].data, "remove") == 0))
        return tired_profiles_mutate(request, bundled_directory, output, error);
    if (request->profile_explain)
        return explain(request, bundled_directory, output, error);
    if (request->arguments.count == 0)
        return tired_error_set(
            error, TIRED_INVALID, "profiles-command",
            "Expected profiles list, show ID, explain -- COMMAND, or validate FILE.", 0);
    const char *operation = request->arguments.items[0].data;
    bool validate = strcmp(operation, "validate") == 0, show = strcmp(operation, "show") == 0,
         list = strcmp(operation, "list") == 0;
    if (!validate && !show && !list)
        return tired_error_set(
            error, TIRED_UNSUPPORTED, "profiles-command",
            "This build supports profiles list, show, explain, and validate; other "
            "profile operations are pending.",
            0);
    if (request->arguments.count != (list ? 1U : 2U))
        return tired_error_set(error, TIRED_INVALID, "profiles-arguments",
                               "Wrong number of profile command arguments.", 0);
    TiredProfileCatalog catalog = {0};
    TiredSettings settings = {0};
    TiredProfile profile = {0};
    TiredText contents = {0}, escaped = {0};
    TiredBuffer buffer;
    tired_buffer_init(&buffer, TIRED_INPUT_LIMIT);
    struct json_object *result = NULL, *entries = NULL;
    if (validate)
    {
        if (!tired_read_file(request->arguments.items[1].data, TIRED_PROFILE_LIMIT, &contents,
                             error) ||
            !tired_profile_parse(contents.data, contents.length, &profile, error))
            goto fail;
        if (request->json)
        {
            result = json_object_new_object();
            if (result == NULL || !add(result, "schema_version", json_object_new_int(1)) ||
                !add(result, "ok", json_object_new_boolean(true)) ||
                !add(result, "command", json_object_new_string("profiles validate")) ||
                !add(result, "profile", json_object_new_string(profile.id)))
                goto allocation;
            if (!json_output(result, output, error))
                goto fail;
        }
        else if (!tired_encode_display(profile.id, strlen(profile.id), &escaped, error) ||
                 !tired_buffer_append(&buffer, "Valid profile: ", 15, error) ||
                 !tired_buffer_append(&buffer, escaped.data, escaped.length, error) ||
                 !tired_buffer_append(&buffer, "\n", 1, error) ||
                 !tired_buffer_take(&buffer, output, error))
            goto fail;
    }
    else
    {
        bool user = tired_spec_choice_is(&request->overrides, TIRED_FIELD_SCOPE, "user");
        if (!tired_config_discover(request, &settings, error) ||
            !tired_profiles_discover_settings(bundled_directory, user, &settings, &catalog, error))
            goto fail;
        if (show)
        {
            if (strcmp(request->arguments.items[1].data, "auto") == 0 ||
                strcmp(request->arguments.items[1].data, "none") == 0)
            {
                tired_error_set(error, TIRED_INVALID, "profile-id",
                                "Show requires a concrete profile ID.", 0);
                goto fail;
            }
            TiredText none = {.data = "", .length = 0};
            const TiredProfileEntry *selected = NULL;
            size_t count;
            if (!tired_catalog_select(&catalog, &none, request->arguments.items[1].data, user,
                                      &selected, &count, error))
                goto fail;
            if (selected == NULL)
            {
                tired_error_set(error, TIRED_NOT_FOUND, "profile-not-found",
                                "Requested profile is unavailable.", 0);
                goto fail;
            }
            if (!json_output(selected->profile.document, output, error))
                goto fail;
        }
        else if (request->json)
        {
            entries = json_object_new_array();
            result = json_object_new_object();
            if (entries == NULL || result == NULL)
                goto allocation;
            for (size_t i = 0; i < catalog.count; ++i)
            {
                if (!tired_catalog_entry_active(&catalog, i, user))
                    continue;
                struct json_object *entry = json_object_new_object();
                const TiredProfileEntry *item = &catalog.items[i];
                if (entry == NULL)
                    goto allocation;
                if (!add(entry, "id", json_object_new_string(item->profile.id)) ||
                    !add(entry, "name", json_object_new_string(item->profile.name)) ||
                    !add(entry, "digest", json_object_new_string(item->digest)) ||
                    !add(entry, "source", json_object_new_string(item->path.data)) ||
                    !add(entry, "origin",
                         json_object_new_string(item->origin == TIRED_PROFILE_BUNDLED ? "bundled"
                                                : item->origin == TIRED_PROFILE_ADMIN
                                                    ? "administrator"
                                                    : "user")))
                {
                    json_object_put(entry);
                    goto allocation;
                }
                if (json_object_array_add(entries, entry) != 0)
                {
                    json_object_put(entry);
                    goto allocation;
                }
            }
            bool ok = add(result, "profiles", entries);
            entries = NULL;
            if (!ok || !add(result, "schema_version", json_object_new_int(1)) ||
                !add(result, "ok", json_object_new_boolean(true)))
                goto allocation;
            if (!json_output(result, output, error))
                goto fail;
        }
        else
        {
            for (size_t i = 0; i < catalog.count; ++i)
            {
                if (!tired_catalog_entry_active(&catalog, i, user))
                    continue;
                const TiredProfileEntry *entry = &catalog.items[i];
                if (!tired_encode_display(entry->profile.name, strlen(entry->profile.name),
                                          &escaped, error) ||
                    !tired_buffer_append(&buffer, entry->profile.id, strlen(entry->profile.id),
                                         error) ||
                    !tired_buffer_append(&buffer, "  ", 2, error) ||
                    !tired_buffer_append(&buffer, escaped.data, escaped.length, error) ||
                    !tired_buffer_append(&buffer, "\n", 1, error))
                    goto fail;
            }
            if (!tired_buffer_take(&buffer, output, error))
                goto fail;
        }
    }
    tired_profile_destroy(&profile);
    tired_settings_destroy(&settings);
    tired_catalog_destroy(&catalog);
    tired_text_destroy(&contents);
    tired_text_destroy(&escaped);
    tired_buffer_destroy(&buffer);
    json_object_put(result);
    json_object_put(entries);
    tired_error_clear(error);
    return true;
allocation:
    tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot allocate profile command output.",
                    0);
fail:
    tired_profile_destroy(&profile);
    tired_settings_destroy(&settings);
    tired_catalog_destroy(&catalog);
    tired_text_destroy(&contents);
    tired_text_destroy(&escaped);
    tired_buffer_destroy(&buffer);
    json_object_put(result);
    json_object_put(entries);
    return false;
}
