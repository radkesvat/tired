#ifndef TIRED_UNIT_FILE_CHANGES_H
#define TIRED_UNIT_FILE_CHANGES_H
#include "tired/value.h"
#include <systemd/sd-bus.h>
typedef struct
{
    TiredText type, path, source;
} TiredUnitFileChange;
typedef struct
{
    bool install_info_known, carries_install_info;
    TiredUnitFileChange *items;
    size_t count;
} TiredUnitFileChanges;
/* Strict EnableUnitFiles ba(sss) or DisableUnitFiles a(sss) reply. <=256 changes,
 * <=1 MiB text, bounded type/path/source. Preserve unknown change types as data.
 * Empty changes are valid. Owned output starts zeroed and is atomic on failure.
 * Reported changes do not authorize unlinking paths or prove current enablement. */
bool tired_unit_file_changes_read(sd_bus_message *message, bool enable,
                                  TiredUnitFileChanges *changes, TiredError *error);
void tired_unit_file_changes_destroy(TiredUnitFileChanges *changes);
#endif
