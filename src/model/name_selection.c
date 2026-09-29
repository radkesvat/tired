#include "tired/name_selection.h"
#include "tired/name.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>

struct TiredNameSelection
{
    TiredText base, candidate;
    TiredTextList directories, pending;
    uint64_t ordinal;
    bool explicit_name;
};
static bool copy_list(TiredTextList *output, const TiredTextList *input, size_t limit,
                      TiredError *error)
{
    for (size_t i = 0; i < input->count; ++i)
        if (!tired_text_list_append(output, input->items[i].data, input->items[i].length, limit,
                                    TIRED_INPUT_LIMIT, error))
            return false;
    return true;
}
bool tired_name_selection_start(const TiredText *base, bool explicit_name,
                                const TiredTextList *directories,
                                const TiredTextList *pending_names, TiredNameSelection **output,
                                TiredError *error)
{
    assert(base != NULL && directories != NULL && pending_names != NULL);
    assert(output != NULL && *output == NULL);
    if (!tired_name_validate_base(base->data, base->length, error))
        return false;
    if (!explicit_name && base->length > TIRED_AUTO_NAME_LIMIT)
        return tired_error_set(error, TIRED_INVALID, "automatic-name-length",
                               "Automatic name bases must contain at most 80 bytes.", 0);
    TiredNameSelection *selection = calloc(1, sizeof(*selection));
    if (selection == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation",
                               "Cannot allocate name selection.", errno);
    selection->ordinal = 1;
    selection->explicit_name = explicit_name;
    if (!tired_text_set(&selection->base, base->data, base->length, TIRED_EXPLICIT_NAME_LIMIT,
                        error) ||
        !tired_name_candidate(base, 1, &selection->candidate, error) ||
        !copy_list(&selection->directories, directories, 256, error) ||
        !copy_list(&selection->pending, pending_names, 4096, error))
        goto failed;
    /* Validate inventory using the common checker. A positive synthetic manager
     * observation avoids filesystem discovery until the real query is supplied. */
    TiredUnitQueryResult validation = {
        .done = true, .file_found = true, .unit_name = selection->candidate.data};
    TiredCollision collision = {0};
    bool valid = tired_collision_check(&selection->candidate, &validation, &selection->directories,
                                       &selection->pending, &collision, error);
    tired_collision_destroy(&collision);
    if (!valid)
        goto failed;
    *output = selection;
    tired_error_clear(error);
    return true;
failed:
    tired_name_selection_destroy(selection);
    return false;
}
const TiredText *tired_name_selection_candidate(const TiredNameSelection *selection)
{
    assert(selection != NULL);
    return &selection->candidate;
}
bool tired_name_selection_observe(TiredNameSelection *selection,
                                  const TiredUnitQueryResult *manager, bool *available,
                                  TiredError *error)
{
    assert(selection != NULL && manager != NULL && available != NULL);
    TiredCollision collision = {0};
    if (!tired_collision_check(&selection->candidate, manager, &selection->directories,
                               &selection->pending, &collision, error))
        return false;
    bool found = collision.kind != TIRED_COLLISION_NONE;
    tired_collision_destroy(&collision);
    if (found)
    {
        if (selection->explicit_name)
            return tired_error_set(error, TIRED_CONFLICT, "explicit-name-collision",
                                   "The explicitly requested service name is already reserved.", 0);
        if (selection->ordinal == UINT64_MAX)
            return tired_error_set(error, TIRED_CONFLICT, "name-space-exhausted",
                                   "No further automatic service name can be generated.", 0);
        if (!tired_name_candidate(&selection->base, selection->ordinal + 1, &selection->candidate,
                                  error))
            return false;
        ++selection->ordinal;
    }
    *available = !found;
    tired_error_clear(error);
    return true;
}
void tired_name_selection_destroy(TiredNameSelection *selection)
{
    if (selection == NULL)
        return;
    tired_text_destroy(&selection->base);
    tired_text_destroy(&selection->candidate);
    tired_text_list_destroy(&selection->directories);
    tired_text_list_destroy(&selection->pending);
    free(selection);
}
