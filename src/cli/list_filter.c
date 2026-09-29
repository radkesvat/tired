#include "tired/list_frontend.h"
#include <assert.h>
#include <string.h>
static unsigned char fold(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c - 'A' + 'a') : c;
}
static bool contains(const char *name, const TiredText *search)
{
    size_t length = strlen(name);
    for (size_t i = 0; i <= length && search->length <= length - i; ++i)
    {
        size_t j = 0;
        for (; j < search->length; ++j)
            if (fold((unsigned char)name[i + j]) != fold((unsigned char)search->data[j]))
                break;
        if (j == search->length)
            return true;
    }
    return false;
}
TiredListMatch tired_list_match(const TiredRequest *request, const char *name, const char *active,
                                const char *enabled, const char *profile)
{
    assert(request != NULL);
    const TiredText *filters[] = {&request->active_filter, &request->enabled_filter,
                                  &request->profile};
    const char *facts[] = {active, enabled, profile};
    bool unknown = false;
    for (size_t i = 0; i < sizeof(filters) / sizeof(filters[0]); ++i)
        if (filters[i]->data != NULL)
        {
            if (facts[i] == NULL)
                unknown = true;
            else if (strcmp(filters[i]->data, facts[i]) != 0)
                return TIRED_LIST_NO_MATCH;
        }
    if (request->search.data != NULL)
    {
        if (name == NULL)
            unknown = true;
        else if (!contains(name, &request->search))
            return TIRED_LIST_NO_MATCH;
    }
    return unknown ? TIRED_LIST_UNKNOWN : TIRED_LIST_MATCH;
}
