#include "tired/journal_selection.h"
#include "tired/name.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void term(TiredJournalClause *clause, const char *field, const char *value)
{
    assert(clause->count < 7 && strlen(field) < 32 && strlen(value) < 256);
    TiredJournalTerm *entry = &clause->terms[clause->count++];
    memcpy(entry->field, field, strlen(field) + 1);
    memcpy(entry->value, value, strlen(value) + 1);
}
bool tired_journal_selection_build(const char *unit, bool user_scope, uid_t uid, const char *boot,
                                   TiredJournalSelection *output, TiredError *error)
{
    assert(unit != NULL && output != NULL);
    size_t length = strnlen(unit, TIRED_EXPLICIT_NAME_LIMIT + 9);
    if (length <= 8 || length > TIRED_EXPLICIT_NAME_LIMIT + 8 ||
        memcmp(unit + length - 8, ".service", 8) != 0 ||
        !tired_name_validate_base(unit, length - 8, error) || (user_scope && uid == (uid_t)-1))
        return tired_error_set(error, TIRED_INVALID, "journal-selection",
                               "Invalid journal unit or user identity.", 0);
    if (boot != NULL)
    {
        if (strnlen(boot, 33) != 32)
            return tired_error_set(error, TIRED_INVALID, "journal-boot",
                                   "Expected a 32-digit lowercase boot ID.", 0);
        for (size_t i = 0; i < 32; ++i)
            if (!((boot[i] >= '0' && boot[i] <= '9') || (boot[i] >= 'a' && boot[i] <= 'f')))
                return tired_error_set(error, TIRED_INVALID, "journal-boot",
                                       "Expected a 32-digit lowercase boot ID.", 0);
    }
    TiredJournalSelection selection = {.count = user_scope ? 3 : 2};
    if (!user_scope)
    {
        term(&selection.clauses[0], "_SYSTEMD_UNIT", unit);
        term(&selection.clauses[1], "UNIT", unit);
        term(&selection.clauses[1], "_UID", "0");
        term(&selection.clauses[1], "_PID", "1");
        term(&selection.clauses[1], "_COMM", "systemd");
    }
    else
    {
        char id[32], manager[64];
        (void)snprintf(id, sizeof(id), "%" PRIuMAX, (uintmax_t)uid);
        (void)snprintf(manager, sizeof(manager), "user@%s.service", id);
        term(&selection.clauses[0], "_SYSTEMD_USER_UNIT", unit);
        term(&selection.clauses[0], "_UID", id);
        const char *executables[] = {"/usr/lib/systemd/systemd", "/lib/systemd/systemd"};
        for (size_t i = 1; i < 3; ++i)
        {
            term(&selection.clauses[i], "USER_UNIT", unit);
            term(&selection.clauses[i], "_UID", id);
            term(&selection.clauses[i], "_SYSTEMD_UNIT", manager);
            term(&selection.clauses[i], "_SYSTEMD_USER_UNIT", "init.scope");
            term(&selection.clauses[i], "_COMM", "systemd");
            term(&selection.clauses[i], "_EXE", executables[i - 1]);
        }
    }
    if (boot != NULL)
        for (size_t i = 0; i < selection.count; ++i)
            term(&selection.clauses[i], "_BOOT_ID", boot);
    *output = selection;
    tired_error_clear(error);
    return true;
}
bool tired_journal_selection_matches(const TiredJournalSelection *selection,
                                     const TiredJournalField *fields, size_t count)
{
    assert(selection != NULL && selection->count > 0 && selection->count <= 3 &&
           (fields != NULL || count == 0));
    for (size_t i = 0; i < selection->count; ++i)
    {
        const TiredJournalClause *clause = &selection->clauses[i];
        bool matches = true;
        for (size_t j = 0; matches && j < clause->count; ++j)
        {
            const TiredJournalTerm *wanted = &clause->terms[j];
            bool found = false;
            for (size_t k = 0; k < count; ++k)
                if (strcmp(fields[k].name, wanted->field) == 0 &&
                    fields[k].value.length == strlen(wanted->value) &&
                    memcmp(fields[k].value.data, wanted->value, fields[k].value.length) == 0)
                {
                    found = true;
                    break;
                }
            matches = found;
        }
        if (matches)
            return true;
    }
    return false;
}
bool tired_journal_selection_apply(sd_journal *journal, const TiredJournalSelection *selection,
                                   TiredError *error)
{
    assert(journal != NULL && selection != NULL && selection->count > 0 && selection->count <= 3);
    int rc = 0;
    for (size_t i = 0; rc >= 0 && i < selection->count; ++i)
    {
        if (i != 0)
            rc = sd_journal_add_disjunction(journal);
        const TiredJournalClause *clause = &selection->clauses[i];
        assert(clause->count > 0 && clause->count <= 7);
        for (size_t j = 0; rc >= 0 && j < clause->count; ++j)
        {
            char match[289];
            int length = snprintf(match, sizeof(match), "%s=%s", clause->terms[j].field,
                                  clause->terms[j].value);
            assert(length > 0 && (size_t)length < sizeof(match));
            rc = sd_journal_add_match(journal, match, (size_t)length);
        }
    }
    if (rc < 0)
        return tired_error_set(
            error, rc == -EACCES || rc == -EPERM ? TIRED_AUTHORIZATION : TIRED_RUNTIME_FAILED,
            "journal-match", "Cannot install scoped journal matches.", -rc);
    tired_error_clear(error);
    return true;
}
