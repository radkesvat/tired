#include "tired/proposal.h"
#include "tired/render.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main(void)
{
    TiredAccount account = {0}, resolved = {0};
    TiredGroup group = {0};
    TiredError error = {0};
    CHECK(tired_account_by_uid(getuid(), &account, &error));
    CHECK(account.uid == getuid());
    CHECK(tired_account_resolve(account.name.data, account.name.length, &resolved, &error));
    CHECK(resolved.uid == account.uid && resolved.primary_group.gid == account.primary_group.gid);
    CHECK(tired_group_resolve(account.primary_group.name.data, account.primary_group.name.length,
                              &group, &error));
    CHECK(group.gid == account.primary_group.gid);
    CHECK(!tired_account_resolve("4294967295", 10, &resolved, &error));
    CHECK(!tired_account_resolve("../bad", 6, &resolved, &error));
    CHECK(!tired_group_resolve("a\nb", 3, &group, &error));
    CHECK(resolved.uid == account.uid);
    TiredTextList args = {0};
    TiredInvocation invocation = {0};
    TiredServiceSpec spec = {0};
    TiredNameBasis basis;
    TiredText unit = {0};
    CHECK(tired_text_list_append(&args, "/proc/self/exe", 14, 4096, TIRED_INPUT_LIMIT, &error));
    CHECK(tired_text_list_append(&args, "", 0, 4096, TIRED_INPUT_LIMIT, &error));
    CHECK(tired_invocation_capture(&args, NULL, &invocation, &error));
    CHECK(tired_proposal_generic(&invocation, false, &spec, &resolved, &basis, &error));
    CHECK(strcmp(spec.fields[TIRED_FIELD_GROUP].value.text.data, account.primary_group.name.data) ==
          0);
    CHECK(strcmp(spec.fields[TIRED_FIELD_WORKING_DIRECTORY].value.text.data,
                 invocation.directory.data) == 0);
    CHECK(spec.fields[TIRED_FIELD_ARGV].value.list.count == 2);
    CHECK(tired_render_unit(&spec, "6d48db39-65c0-4de0-ae2a-c5e0208ea1d7", NULL, NULL, &unit,
                            &error));
    CHECK(strstr(unit.data, "ExecStart=:\"/proc/self/exe\" \"\"") != NULL);
    CHECK(tired_text_set(&invocation.argv.items[0], "foo.service", 11, 256, &error));
    CHECK(tired_proposal_generic(&invocation, true, &spec, &resolved, &basis, &error));
    CHECK(strcmp(spec.fields[TIRED_FIELD_NAME].value.text.data, "foo.service") == 0);
    CHECK(tired_spec_choice_is(&spec, TIRED_FIELD_WANTED_BY, "default.target"));
    CHECK(tired_render_unit(&spec, "6d48db39-65c0-4de0-ae2a-c5e0208ea1d7", NULL, NULL, &unit,
                            &error));
    CHECK(strstr(unit.data, "\nUser=") == NULL);
    tired_account_destroy(&account);
    tired_account_destroy(&resolved);
    tired_group_destroy(&group);
    tired_text_list_destroy(&args);
    tired_invocation_destroy(&invocation);
    tired_spec_destroy(&spec);
    tired_text_destroy(&unit);
    return 0;
}
