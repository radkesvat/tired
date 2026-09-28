#include "tired/proposal.h"
#include "tired/encode.h"
#include <assert.h>

static bool captured(TiredServiceSpec *spec, TiredFieldId id, const TiredText *text,
                     TiredError *error)
{
    return tired_spec_set(spec, id, text->data, text->length, TIRED_ORIGIN_CAPTURE, true, error);
}

bool tired_proposal_generic(const TiredInvocation *invocation, bool user_scope,
                            TiredServiceSpec *spec, TiredAccount *invoking_account,
                            TiredNameBasis *name_basis, TiredError *error)
{
    assert(invocation != NULL && spec != NULL && invoking_account != NULL && name_basis != NULL);
    TiredServiceSpec proposal = {0};
    TiredAccount account = {0};
    TiredText name = {0}, description = {0};
    TiredNameBasis basis;
    TiredBuffer buffer;
    tired_buffer_init(&buffer, 4096);
    if (!tired_account_by_uid(invocation->uid, &account, error) ||
        !tired_spec_defaults(&proposal, error) ||
        !tired_name_suggest(&invocation->argv, &name, &basis, error))
        goto fail;
    if (user_scope &&
        !tired_spec_set(&proposal, TIRED_FIELD_SCOPE, "user", 4, TIRED_ORIGIN_USER, true, error))
        goto fail;
    if (!captured(&proposal, TIRED_FIELD_NAME, &name, error) ||
        !captured(&proposal, TIRED_FIELD_RUN_AS, &account.name, error) ||
        !captured(&proposal, TIRED_FIELD_GROUP, &account.primary_group.name, error) ||
        !captured(&proposal, TIRED_FIELD_EXECUTABLE, &invocation->executable, error) ||
        !captured(&proposal, TIRED_FIELD_WORKING_DIRECTORY, &invocation->directory, error) ||
        !captured(&proposal, TIRED_FIELD_SYSLOG_IDENTIFIER, &name, error))
        goto fail;
    for (size_t i = 0; i < invocation->argv.count; ++i)
        if (!tired_spec_append(&proposal, TIRED_FIELD_ARGV, invocation->argv.items[i].data,
                               invocation->argv.items[i].length, TIRED_ORIGIN_CAPTURE, error))
            goto fail;
    if (!tired_buffer_append(&buffer, name.data, name.length, error) ||
        !tired_buffer_append(&buffer, " (managed by tired)", sizeof(" (managed by tired)") - 1,
                             error) ||
        !tired_buffer_take(&buffer, &description, error) ||
        !tired_spec_set(&proposal, TIRED_FIELD_DESCRIPTION, description.data, description.length,
                        TIRED_ORIGIN_DEFAULT, false, error) ||
        !tired_spec_resolve_scope(&proposal, error) ||
        !tired_spec_resolve_retry(&proposal, error) ||
        !tired_spec_validate_scalars(&proposal, error))
        goto fail;
    tired_spec_destroy(spec);
    *spec = proposal;
    tired_account_destroy(invoking_account);
    *invoking_account = account;
    *name_basis = basis;
    tired_text_destroy(&name);
    tired_text_destroy(&description);
    tired_buffer_destroy(&buffer);
    tired_error_clear(error);
    return true;
fail:
    tired_spec_destroy(&proposal);
    tired_account_destroy(&account);
    tired_text_destroy(&name);
    tired_text_destroy(&description);
    tired_buffer_destroy(&buffer);
    return false;
}
