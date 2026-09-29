#include "tired/io.h"
#include "tired/stage.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
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
int main(void)
{
    int result = 1;
    TiredStage *stage = NULL, *other = NULL;
    TiredText base = {.data = "precise.service", .length = 15}, contents = {0};
    TiredText path = {0}, directory = {0};
    TiredError error = {0};
    const char *unit = "[Service]\nExecStart=/usr/bin/true\n";
    CHECK(tired_stage_create(&base, unit, strlen(unit), &stage, &error));
    CHECK(strstr(tired_stage_unit_path(stage), "/precise.service.service") != NULL);
    CHECK(tired_stage_create(&base, unit, strlen(unit), &other, &error));
    CHECK(strcmp(tired_stage_directory(stage), tired_stage_directory(other)) != 0);
    CHECK(tired_stage_remove(other, &error));
    tired_stage_destroy(other);
    other = NULL;
    struct stat status;
    CHECK(stat(tired_stage_unit_path(stage), &status) == 0 && (status.st_mode & 0777) == 0600);
    CHECK(stat(tired_stage_directory(stage), &status) == 0 && (status.st_mode & 0777) == 0700);
    CHECK(tired_read_file(tired_stage_unit_path(stage), 1024, &contents, &error));
    CHECK(strcmp(contents.data, unit) == 0);
    CHECK(tired_text_set(&directory, tired_stage_directory(stage),
                         strlen(tired_stage_directory(stage)), 1024, &error));
    CHECK(tired_path_absolute(&directory, "retained", 8, &path, &error));
    CHECK(rename(tired_stage_unit_path(stage), path.data) == 0);
    CHECK(tired_write_private_new(tired_stage_unit_path(stage), "replacement", 11, &error));
    CHECK(!tired_stage_remove(stage, &error));
    CHECK(error.status == TIRED_CONFLICT);
    CHECK(tired_read_file(tired_stage_unit_path(stage), 1024, &contents, &error));
    CHECK(strcmp(contents.data, "replacement") == 0);
    CHECK(unlink(tired_stage_unit_path(stage)) == 0);
    CHECK(rename(path.data, tired_stage_unit_path(stage)) == 0);
    CHECK(tired_stage_remove(stage, &error));
    CHECK(tired_stage_remove(stage, &error));
    CHECK(access(directory.data, F_OK) != 0);
    tired_stage_destroy(stage);
    stage = NULL;
    base = (TiredText){.data = "../escape", .length = 9};
    CHECK(!tired_stage_create(&base, unit, strlen(unit), &stage, &error));
    CHECK(stage == NULL);
    base = (TiredText){.data = "valid", .length = 5};
    CHECK(!tired_stage_create(&base, "a\0b", 3, &stage, &error));
    CHECK(stage == NULL);
    result = 0;
cleanup:
    if (stage != NULL)
    {
        (void)tired_stage_remove(stage, &error);
        tired_stage_destroy(stage);
    }
    if (other != NULL)
    {
        (void)tired_stage_remove(other, &error);
        tired_stage_destroy(other);
    }
    tired_text_destroy(&contents);
    tired_text_destroy(&path);
    tired_text_destroy(&directory);
    return result;
}
