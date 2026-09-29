#include "tired/io.h"
#include "tired/service_record.h"
#include <stdio.h>
#include <string.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #expression,                   \
                    error.code == NULL ? "none" : error.code);                                     \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
static bool set(TiredText *output, const char *text, TiredError *error)
{
    return tired_text_set(output, text, strlen(text), TIRED_INPUT_LIMIT, error);
}
int main(int argc, char **argv)
{
    int result = 1;
    TiredServiceRecord
        source =
            {.metadata = {.service_uuid = "01234567-89ab-4cde-8fab-0123456789ab",
                          .revision_uuid = "11234567-89ab-4cde-8fab-0123456789ab",
                          .transaction_uuid = "21234567-89ab-4cde-8fab-0123456789ab",
                          .unit_sha256 =
                              "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                          .user_scope = true,
                          .owner_uid = 1000,
                          .invoking_uid = 1000,
                          .service_uid = 1000,
                          .invoking_gid = 1000,
                          .service_gid = 1000,
                          .created_usec = 1,
                          .updated_usec = 2},
             .review = {.approved_sha256 =
                            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                        .argument_count = 2},
             .has_environment = true,
             .environment_revision = "31234567-89ab-4cde-8fab-0123456789ab",
             .environment_sha256 =
                 "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"},
        parsed = {0};
    TiredLayout layout = {0};
    TiredText encoded = {0}, again = {0};
    TiredError error = {0};
    struct json_object *document = NULL, *review = NULL;
    CHECK(set(&source.metadata.unit_name, "relay.service", &error));
    CHECK(set(&source.metadata.writer_version, "0.1.0", &error));
    CHECK(tired_spec_defaults(&source.spec, &error));
    CHECK(tired_spec_set(&source.spec, TIRED_FIELD_NAME, "relay", 5, TIRED_ORIGIN_USER, true,
                         &error));
    CHECK(tired_spec_set(&source.spec, TIRED_FIELD_SCOPE, "user", 4, TIRED_ORIGIN_USER, true,
                         &error));
    CHECK(tired_spec_resolve_scope(&source.spec, &error));
    CHECK(tired_spec_set(&source.spec, TIRED_FIELD_EXECUTABLE, "/missing/app", 12,
                         TIRED_ORIGIN_CAPTURE, true, &error));
    CHECK(tired_spec_set(&source.spec, TIRED_FIELD_WORKING_DIRECTORY, "/missing", 8,
                         TIRED_ORIGIN_CAPTURE, true, &error));
    CHECK(tired_spec_append(&source.spec, TIRED_FIELD_ARGV, "/missing/app", 12,
                            TIRED_ORIGIN_CAPTURE, &error));
    CHECK(tired_spec_append(&source.spec, TIRED_FIELD_ARGV, "secret", 6, TIRED_ORIGIN_CAPTURE,
                            &error));
    source.review.sensitive_arguments[1] = true;
    CHECK(set(&source.executable.lexical_path, "/missing/app", &error));
    CHECK(set(&source.executable.resolved_path, "/missing/resolved", &error));
    source.executable.device = 1;
    source.executable.inode = 2;
    CHECK(tired_environment_set(&source.environment, "TOKEN=hidden", 12, TIRED_ENV_EXPLICIT, true,
                                &error));
    CHECK(tired_credentials_add(&source.credentials, "key=/missing/key", 16, &error));
    CHECK(set(&source.unit_path, "/fixture/config/systemd/user/relay.service", &error));
    CHECK(set(&source.environment_path,
              "/fixture/config/tired/services/01234567-89ab-4cde-8fab-0123456789ab/revisions/"
              "31234567-89ab-4cde-8fab-0123456789ab/environment",
              &error));
    CHECK(tired_text_list_append(&source.external_config_paths, "/missing/app.toml", 17, 256,
                                 TIRED_INPUT_LIMIT, &error));
    CHECK(tired_layout_resolve(true, "/fixture", "/fixture/config", "/fixture/state",
                               "/fixture/run", &layout, &error));
    CHECK(tired_service_record_check_layout(&source, &layout, &error));
    CHECK(tired_service_record_encode(&source, &encoded, &error));
    CHECK(tired_service_record_parse(encoded.data, encoded.length, &parsed, &error));
    CHECK(parsed.metadata.owner_uid == 1000 && parsed.review.sensitive_arguments[1] &&
          parsed.has_environment && !parsed.has_profile);
    CHECK(parsed.environment.count == 1 && parsed.credentials.count == 1 &&
          parsed.external_config_paths.count == 1);
    CHECK(tired_service_record_check_layout(&parsed, &layout, &error));
    CHECK(tired_service_record_encode(&parsed, &again, &error));
    CHECK(strcmp(encoded.data, again.data) == 0);
    CHECK(tired_json_parse(encoded.data, encoded.length, TIRED_SERVICE_RECORD_LIMIT, &document,
                           &error));
    CHECK(json_object_object_get_ex(document, "review", &review));
    CHECK(json_object_object_add(review, "argument_count", json_object_new_int(3)) == 0);
    const char *bad = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_service_record_parse(bad, strlen(bad), &parsed, &error));
    CHECK(parsed.review.argument_count == 2);
    CHECK(json_object_object_add(review, "argument_count", json_object_new_int(2)) == 0);
    CHECK(json_object_object_add(document, "owned_environment", NULL) == 0);
    bad = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_service_record_parse(bad, strlen(bad), &parsed, &error));
    CHECK(set(&source.unit_path, "/elsewhere/relay.service", &error));
    CHECK(!tired_service_record_check_layout(&source, &layout, &error));
    CHECK(set(&source.unit_path, parsed.unit_path.data, &error));
    CHECK(argc == 2 && tired_read_file(argv[1], TIRED_PROFILE_LIMIT, &again, &error));
    CHECK(tired_profile_parse(again.data, again.length, &source.profile.profile, &error));
    CHECK(source.profile.profile.count == 0);
    CHECK(set(&source.profile.source_path, argv[1], &error));
    memcpy(source.profile.source_sha256, source.metadata.unit_sha256, 65);
    source.has_profile = true;
    tired_environment_destroy(&source.environment);
    source.has_environment = false;
    tired_text_destroy(&source.environment_path);
    source.environment_revision[0] = source.environment_sha256[0] = '\0';
    CHECK(tired_service_record_encode(&source, &encoded, &error));
    CHECK(tired_service_record_parse(encoded.data, encoded.length, &parsed, &error));
    CHECK(parsed.has_profile && !parsed.has_environment &&
          strcmp(parsed.profile.profile.id, "generic") == 0);
    result = 0;
cleanup:
    json_object_put(document);
    tired_service_record_destroy(&source);
    tired_service_record_destroy(&parsed);
    tired_layout_destroy(&layout);
    tired_text_destroy(&encoded);
    tired_text_destroy(&again);
    return result;
}
