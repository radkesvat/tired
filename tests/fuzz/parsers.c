#include "tired/encode.h"
#include "tired/environment.h"
#include "tired/json.h"
#include "tired/model_format.h"
#include "tired/mutation.h"
#include "tired/name.h"
#include "tired/settings.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size == 0 || size > TIRED_INPUT_LIMIT)
        return 0;
    unsigned kind = data[0] % 10;
    const char *bytes = (const char *)data + 1;
    size_t length = size - 1;
    TiredError error = {0};
    TiredText text = {0};
    switch (kind)
    {
    case 0:
    {
        TiredProfile profile = {0};
        (void)tired_profile_parse(bytes, length, &profile, &error);
        tired_profile_destroy(&profile);
        break;
    }
    case 1:
    {
        TiredSettings settings = {0};
        (void)tired_settings_parse(bytes, length, &settings, &error);
        tired_settings_destroy(&settings);
        break;
    }
    case 2:
    {
        TiredMutation mutation = {0};
        if (tired_mutation_parse(bytes, length, &mutation, &error))
            (void)tired_mutation_encode(&mutation, &text, &error);
        tired_mutation_destroy(&mutation);
        break;
    }
    case 3:
    {
        TiredEnvironment environment = {0};
        if (tired_environment_import(&environment, bytes, length, &error))
            (void)tired_environment_encode(&environment, &text, &error);
        tired_environment_destroy(&environment);
        break;
    }
    case 4:
        (void)tired_name_explicit(bytes, length, &text, &error);
        break;
    case 5:
        (void)tired_encode_token(bytes, length, &text, &error);
        break;
    case 6:
    {
        TiredServiceSpec spec = {0};
        if (tired_spec_parse(bytes, length, &spec, &error))
            (void)tired_spec_encode(&spec, &text, &error);
        tired_spec_destroy(&spec);
        break;
    }
    case 8:
    {
        TiredServiceRecord record = {0};
        if (tired_service_record_parse(bytes, length, &record, &error))
            (void)tired_service_record_encode(&record, &text, &error);
        tired_service_record_destroy(&record);
        break;
    }
    case 9:
    {
        char *input = malloc(length + 1);
        if (input == NULL)
            break;
        memcpy(input, bytes, length);
        input[length] = '\0';
        const char *arguments[128] = {"tired"};
        int count = 1;
        size_t offset = 0;
        while (offset < length && count < (int)(sizeof(arguments) / sizeof(arguments[0])))
        {
            arguments[count++] = input + offset;
            offset += strlen(input + offset) + 1;
        }
        TiredRequest request = {0};
        bool json = false;
        (void)tired_cli_parse_format(count, arguments, &request, &json, &error);
        tired_request_destroy(&request);
        free(input);
        break;
    }
    default:
    {
        struct json_object *document = NULL;
        (void)tired_json_parse(bytes, length, TIRED_INPUT_LIMIT, &document, &error);
        json_object_put(document);
        break;
    }
    }
    tired_text_destroy(&text);
    return 0;
}
