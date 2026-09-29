#include "tired/journal_selection.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
#define FIELD(key, text) {.name = key, .value = {.data = text, .length = sizeof(text) - 1}}
int main(void)
{
    int result = 1;
    TiredJournalSelection selection = {0};
    TiredError error = {0};
    sd_journal *journal = NULL;
    char fixture[] = "journal-selection-XXXXXX";
    char *directory = NULL;
    TiredJournalField fields[] = {FIELD("_SYSTEMD_UNIT", "relay.service"),
                                  FIELD("_UID", "1000"),
                                  FIELD("UNIT", "relay.service"),
                                  FIELD("_PID", "99"),
                                  FIELD("_COMM", "app"),
                                  FIELD("_SYSTEMD_USER_UNIT", "relay.service"),
                                  FIELD("USER_UNIT", "relay.service"),
                                  FIELD("_EXE", "/usr/lib/systemd/systemd"),
                                  FIELD("_BOOT_ID", "0123456789abcdef0123456789abcdef")};
    CHECK(tired_journal_selection_build("relay.service", false, 0, NULL, &selection, &error));
    CHECK(tired_journal_selection_matches(&selection, fields, 2));      /* nonroot system service */
    CHECK(!tired_journal_selection_matches(&selection, fields + 2, 3)); /* injected UNIT */
    fields[1].value = (TiredText){.data = "0", .length = 1};
    fields[3].value = (TiredText){.data = "1", .length = 1};
    fields[4].value = (TiredText){.data = "systemd", .length = 7};
    CHECK(tired_journal_selection_matches(&selection, fields + 1, 4));
    fields[3].value = (TiredText){.data = "99", .length = 2};
    CHECK(!tired_journal_selection_matches(&selection, fields + 1, 4));
    CHECK(tired_journal_selection_build("relay.service", true, 1000, NULL, &selection, &error));
    fields[1].value = (TiredText){.data = "1000", .length = 4};
    CHECK(tired_journal_selection_matches(&selection, fields, 9));
    fields[1].value = (TiredText){.data = "2000", .length = 4};
    CHECK(!tired_journal_selection_matches(&selection, fields, 9));
    fields[1].value = (TiredText){.data = "1000", .length = 4};
    fields[5].value = (TiredText){.data = "other.service", .length = 13};
    CHECK(!tired_journal_selection_matches(&selection, fields, 9)); /* USER_UNIT alone */
    fields[0].value = (TiredText){.data = "user@1000.service", .length = 17};
    fields[5].value = (TiredText){.data = "init.scope", .length = 10};
    CHECK(tired_journal_selection_matches(&selection, fields, 9));
    fields[7].value = (TiredText){.data = "/bin/forged", .length = 11};
    CHECK(!tired_journal_selection_matches(&selection, fields, 9));
    fields[7].value = (TiredText){.data = "/lib/systemd/systemd", .length = 20};
    fields[7].value.length = strlen(fields[7].value.data);
    CHECK(tired_journal_selection_matches(&selection, fields, 9));
    CHECK(tired_journal_selection_build("relay.service", true, 1000,
                                        "0123456789abcdef0123456789abcdef", &selection, &error));
    CHECK(tired_journal_selection_matches(&selection, fields, 9));
    CHECK(!tired_journal_selection_matches(&selection, fields, 8));
    fields[8].value = (TiredText){.data = "1123456789abcdef0123456789abcdef", .length = 32};
    CHECK(!tired_journal_selection_matches(&selection, fields, 9));
    TiredJournalSelection saved = selection;
    CHECK(!tired_journal_selection_build("../relay.service", false, 0, NULL, &selection, &error));
    CHECK(
        !tired_journal_selection_build("relay.service", true, (uid_t)-1, NULL, &selection, &error));
    CHECK(!tired_journal_selection_build("relay.service", false, 0, "not-a-boot", &selection,
                                         &error));
    CHECK(memcmp(&selection, &saved, sizeof(saved)) == 0);
    directory = mkdtemp(fixture);
    CHECK(directory != NULL);
    CHECK(sd_journal_open_directory(&journal, directory, 0) >= 0);
    CHECK(tired_journal_selection_apply(journal, &selection, &error));
    CHECK(sd_journal_seek_head(journal) >= 0 && sd_journal_next(journal) == 0);
    result = 0;
cleanup:
    sd_journal_close(journal);
    if (directory != NULL)
        (void)rmdir(directory);
    return result;
}
