#include "tired/config_frontend.h"
#include "tired/io.h"
#include "tired/list_frontend.h"
#include "tired/logs_frontend.h"
#include "tired/name.h"
#include "tired/plan_output.h"
#include "tired/profile_frontend.h"
#include "tired/recover_frontend.h"
#include "tired/show_frontend.h"
#include "tired/status_frontend.h"
#include <errno.h>
#include <json-c/json.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static bool error_field(struct json_object *object, const char *key, const char *text)
{
    struct json_object *value = json_object_new_string(text);
    if (value == NULL)
        return false;
    if (json_object_object_add(object, key, value) == 0)
        return true;
    json_object_put(value);
    return false;
}
static volatile sig_atomic_t logs_cancelled;
static void cancel_logs(int signal_number) { logs_cancelled = signal_number; }
static bool run_logs(const TiredRequest *request, TiredStatus *status, TiredError *error)
{
    const int signals[] = {SIGINT, SIGTERM, SIGHUP};
    struct sigaction old[3], action = {.sa_handler = cancel_logs};
    sigemptyset(&action.sa_mask);
    size_t installed = 0;
    bool ok = false;
    logs_cancelled = 0;
    for (; installed < 3; ++installed)
        if (sigaction(signals[installed], &action, &old[installed]) != 0)
        {
            tired_error_set(error, TIRED_INTERNAL, "logs-signals",
                            "Cannot install journal cancellation handlers.", errno);
            goto done;
        }
    ok = tired_logs_command(request, stdout, &logs_cancelled, status, error);
done:
    while (installed != 0)
    {
        --installed;
        (void)sigaction(signals[installed], &old[installed], NULL);
    }
    return ok;
}
static void report_error(const TiredError *error, bool json, const TiredRequest *request)
{
    if (!json)
    {
        fprintf(stderr, "tired: %s [%s]\n",
                error->message == NULL ? "Operation failed." : error->message,
                error->code == NULL ? "internal" : error->code);
        return;
    }
    struct json_object *object = json_object_new_object();
    if (object == NULL)
    {
        fputs("{\"ok\":false,\"exit_code\":1,\"error\":\"allocation\"}\n", stdout);
        return;
    }
    struct json_object *status = json_object_new_int(error->status);
    struct json_object *ok = json_object_new_boolean(false);
    struct json_object *schema = json_object_new_int(1);
    struct json_object *code =
        json_object_new_string(error->code == NULL ? "internal" : error->code);
    struct json_object *message =
        json_object_new_string(error->message == NULL ? "Operation failed." : error->message);
    if (status == NULL || code == NULL || message == NULL || ok == NULL || schema == NULL)
    {
        json_object_put(status);
        json_object_put(code);
        json_object_put(message);
        json_object_put(ok);
        json_object_put(schema);
        json_object_put(object);
        fputs("{\"ok\":false,\"exit_code\":1,\"error\":\"allocation\"}\n", stdout);
        return;
    }
    /* Fixed literal keys; json-c owns successful insertions. */
    int result = json_object_object_add(object, "exit_code", status);
    if (result != 0)
        json_object_put(status);
    int rc = json_object_object_add(object, "error", code);
    if (rc != 0)
        json_object_put(code);
    result |= rc;
    if (request->command == TIRED_COMMAND_LOGS)
    {
        TiredText base = {0}, unit = {0};
        TiredError ignored = {0};
        const char *selected = "unknown";
        if (request->arguments.count == 1)
        {
            const TiredText *operand = &request->arguments.items[0];
            selected = operand->data;
            if (tired_name_explicit(operand->data, operand->length, &base, &ignored) &&
                tired_name_candidate(&base, 1, &unit, &ignored))
                selected = unit.data;
        }
        if (!error_field(object, "event_type", "journal_error") ||
            !error_field(object, "selected_service", selected) ||
            !error_field(object, "scope",
                         tired_spec_choice_is(&request->overrides, TIRED_FIELD_SCOPE, "user")
                             ? "user"
                             : "system"))
            result = -1;
        if (tired_spec_choice_is(&request->overrides, TIRED_FIELD_SCOPE, "user"))
        {
            struct json_object *uid = json_object_new_uint64(getuid());
            if (uid == NULL || json_object_object_add(object, "selected_uid", uid) != 0)
            {
                json_object_put(uid);
                result = -1;
            }
        }
        tired_text_destroy(&base);
        tired_text_destroy(&unit);
    }
    rc = json_object_object_add(object, "ok", ok);
    if (rc != 0)
        json_object_put(ok);
    result |= rc;
    rc = json_object_object_add(object, "schema_version", schema);
    if (rc != 0)
        json_object_put(schema);
    result |= rc;
    rc = json_object_object_add(object, "message", message);
    if (rc != 0)
        json_object_put(message);
    result |= rc;
    const char *encoded =
        result == 0 ? json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN) : NULL;
    fputs(encoded == NULL ? "{\"ok\":false,\"exit_code\":1,\"error\":\"allocation\"}" : encoded,
          stdout);
    fputc('\n', stdout);
    json_object_put(object);
}
static bool allowed(const TiredRequest *request, const char *risk)
{
    for (size_t i = 0; i < request->allowed_risks.count; ++i)
        if (strcmp(request->allowed_risks.items[i].data, risk) == 0)
            return true;
    return false;
}

int main(int argc, char **argv)
{
    (void)signal(SIGPIPE, SIG_IGN);
    TiredRequest request = {0};
    TiredPlan plan = {0};
    TiredSettings settings = {0};
    TiredProfileCatalog catalog = {0};
    TiredText output = {0};
    TiredError error = {0};
    bool json = false;
    int result = TIRED_OK;
    if (!tired_cli_parse_format(argc, (const char *const *)argv, &request, &json, &error))
        goto failed;
    if (request.help)
    {
        fputs(
            "Usage: tired plan --offline [--profile auto|none|ID] [options] -- COMMAND [ARG...]\n"
            "       tired profiles list | show ID | validate FILE [--json]\n"
            "       tired profiles explain [options] -- COMMAND [ARG...]\n"
            "       tired config show | validate FILE [--user] [--json]\n"
            "       tired recover [--user] [--json]  (read-only inspection)\n"
            "       tired status NAME [--user] [--json] [--check-active]\n"
            "       tired list [--user] [--json]\n"
            "         [--active-state STATE] [--enabled-state STATE] [--profile ID] [--search "
            "TEXT]\n"
            "       tired show NAME [--user] [--json | --unit] [--effective] [--output NEW_FILE]\n"
            "       tired logs NAME [--user] [--json] [--follow] [--lines 0..10000]\n"
            "         [--since @SECONDS|UTC_TIMESTAMP] [--boot current|BOOT_ID]\n"
            "       tired --help | --version\n\n"
            "Offline planning and profile, configuration, and stored-journal inspection are "
            "available.\n"
            "Service installation, live validation, and recovery resolutions are still under "
            "implementation.\n"
            "Options: --user, --name NAME, --run-as USER, --group GROUP,\n"
            "  --working-directory PATH, --type TYPE, --restart POLICY, --restart-sec TIME,\n"
            "  --retry-policy persistent|limited, --nofile SOFT:HARD, --set FIELD=VALUE, --unset "
            "FIELD,\n"
            "  --env KEY=VALUE, --pass-env KEY, --env-file PATH, --import-env-file PATH,\n"
            "  --credential NAME=PATH, --unit, --json, --output NEW_FILE.\n"
            "  --sensitive-arg INDEX masks a workload argument (1 is the first after COMMAND).\n"
            "Sensitive exports: --include-sensitive --allow-risk sensitive-export --output "
            "NEW_FILE.\n"
            "Arguments after COMMAND or -- belong to the workload. Planning never executes it.\n",
            stdout);
        goto done;
    }
    if (request.version)
    {
        printf("tired %s\n", TIRED_VERSION);
        goto done;
    }
    if (request.command != TIRED_COMMAND_PLAN || !request.offline)
    {
        if (request.command == TIRED_COMMAND_LOGS)
        {
            TiredStatus status = TIRED_OK;
            if (!run_logs(&request, &status, &error))
            {
                if (error.code != NULL && strcmp(error.code, "logs-output-write") == 0)
                    json = false;
                goto failed;
            }
            result = status;
            goto done;
        }
        if (request.command == TIRED_COMMAND_PROFILES || request.command == TIRED_COMMAND_CONFIG ||
            request.command == TIRED_COMMAND_RECOVER || request.command == TIRED_COMMAND_STATUS ||
            request.command == TIRED_COMMAND_LIST || request.command == TIRED_COMMAND_SHOW)
        {
            TiredStatus command_status = TIRED_OK;
            bool ok = request.command == TIRED_COMMAND_SHOW
                          ? tired_show_command(&request, &output, &command_status, &error)
                      : request.command == TIRED_COMMAND_LIST
                          ? tired_list_command(&request, &output, &command_status, &error)
                      : request.command == TIRED_COMMAND_STATUS
                          ? tired_status_command(&request, &output, &command_status, &error)
                      : request.command == TIRED_COMMAND_RECOVER
                          ? tired_recover_command(&request, &output, &command_status, &error)
                      : request.command == TIRED_COMMAND_CONFIG
                          ? tired_config_command(&request, &output, &error)
                          : tired_profiles_command(&request, TIRED_BUNDLED_PROFILE_DIRECTORY,
                                                   &output, &error);
            if (!ok)
                goto failed;
            result = command_status;
            if (fwrite(output.data, 1, output.length, stdout) != output.length)
            {
                json = false;
                tired_error_set(&error, TIRED_INTERNAL, "output-write",
                                "Cannot write command output.", 0);
                goto failed;
            }
            goto done;
        }
        tired_error_set(
            &error, TIRED_UNSUPPORTED, "implementation-incomplete",
            "This command is not implemented in this build; use --help for available commands.", 0);
        goto failed;
    }
    if (request.include_sensitive &&
        (request.output.data == NULL || !allowed(&request, "sensitive-export")))
    {
        tired_error_set(&error, TIRED_INVALID, "sensitive-export",
                        "Sensitive output requires a new private output file and explicit "
                        "sensitive-export acknowledgment.",
                        0);
        goto failed;
    }
    if (!tired_config_discover(&request, &settings, &error) ||
        !tired_plan_prepare_settings(&request, &settings, &plan, &error))
        goto failed;
    if (request.profile.data == NULL || strcmp(request.profile.data, "none") != 0)
    {
        bool user = tired_spec_choice_is(&plan.spec, TIRED_FIELD_SCOPE, "user");
        TiredProfileContext context = {
            .systemd_version = 249}; /* Declared offline target, not a host observation. */
        if (!tired_profiles_discover_settings(TIRED_BUNDLED_PROFILE_DIRECTORY, user, &settings,
                                              &catalog, &error) ||
            !tired_plan_apply_profiles(&plan, &catalog, request.profile.data, &context, &error))
            goto failed;
    }
    if (!tired_plan_output(&plan, request.json, request.unit, request.include_sensitive, &output,
                           &error))
        goto failed;
    if (request.output.data != NULL)
    {
        if (!tired_write_private_new(request.output.data, output.data, output.length, &error))
            goto failed;
        if (request.json)
            fputs("{\"schema_version\":1,\"command\":\"plan\",\"ok\":true,\"exit_code\":0,"
                  "\"exported\":true}\n",
                  stdout);
        else if (!request.quiet)
            fputs("Wrote a private offline plan. Live validation was not performed.\n", stdout);
    }
    else if (fwrite(output.data, 1, output.length, stdout) != output.length)
    {
        tired_error_set(&error, TIRED_INTERNAL, "output-write", "Cannot write plan output.", 0);
        /* A partial document cannot be repaired by emitting another document. */
        json = false;
        goto failed;
    }
    goto done;
failed:
    result = error.status == TIRED_OK ? TIRED_INTERNAL : error.status;
    report_error(&error, json, &request);
done:
    tired_text_destroy(&output);
    tired_plan_destroy(&plan);
    tired_settings_destroy(&settings);
    tired_catalog_destroy(&catalog);
    tired_request_destroy(&request);
    if (fflush(stdout) != 0)
        return TIRED_INTERNAL;
    return result;
}
