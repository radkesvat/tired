#include "tired/load_paths.h"
#include "tired/manager.h"
#include "tired/unit_query.h"
#include <stdio.h>
#include <time.h>
static void tick(void)
{
    struct timespec delay = {.tv_nsec = 1000000};
    (void)nanosleep(&delay, NULL);
}
int main(void)
{
    sd_bus *bus = NULL;
    TiredManagerIdentity *identity = NULL;
    TiredManagerProbe *probe = NULL;
    TiredUnitQuery *query = NULL;
    TiredLoadPaths *paths = NULL;
    TiredError error = {0};
    int result = 1;
    if (!tired_manager_bus_open(false, &bus, &error))
    {
        if (error.status == TIRED_NOT_FOUND || error.status == TIRED_AUTHORIZATION)
            result = 77;
        goto done;
    }
    if (!tired_manager_identity_start(bus, false, 3000, &identity, &error))
        goto done;
    for (unsigned i = 0; i < 5000 && !tired_manager_identity_step(identity); ++i)
        tick();
    TiredManagerIdentityResult owner = tired_manager_identity_result(identity);
    error = owner.error;
    if (!owner.ready)
    {
        if (error.status == TIRED_NOT_FOUND || error.status == TIRED_AUTHORIZATION)
            result = 77;
        goto done;
    }
    if (!tired_manager_probe_start_unique(bus, owner.unique_name, 3000, &probe, &error))
        goto done;
    for (unsigned i = 0; i < 5000 && !tired_manager_probe_step(probe); ++i)
        tick();
    TiredManagerProbeResult version = tired_manager_probe_result(probe);
    error = version.error;
    if (!version.done || error.status != TIRED_OK)
        goto done;
    if (!tired_load_paths_start(identity, 3000, &paths, &error))
        goto done;
    for (unsigned i = 0; i < 5000 && !tired_load_paths_step(paths); ++i)
        tick();
    TiredLoadPathsResult locations = tired_load_paths_result(paths);
    error = locations.error;
    if (!locations.done || error.status != TIRED_OK || locations.directories == NULL)
        goto done;
    TiredText name = {.data = "systemd-journald", .length = 16};
    if (!tired_unit_query_start(identity, &name, 3000, &query, &error))
        goto done;
    for (unsigned i = 0; i < 5000 && !tired_unit_query_step(query); ++i)
        tick();
    TiredUnitQueryResult observed = tired_unit_query_result(query);
    error = observed.error;
    if (!observed.done || error.status != TIRED_OK || observed.observation == NULL)
        goto done;
    if (observed.object_found && !observed.observation->fields[TIRED_OBS_ID].known)
        goto done;
    printf("Manager %s; unit file state: %s; manager object: %s; PID property: %s\n",
           version.version, observed.file_state == NULL ? "absent" : observed.file_state,
           observed.object_found ? "present" : "absent",
           observed.observation->fields[TIRED_OBS_MAIN_PID].known ? "known" : "unknown");
    printf("Manager load locations: %zu\n", locations.directories->count);
    result = 0;
done:
    tired_load_paths_destroy(paths);
    if (result != 0)
        fprintf(stderr, "Live manager query %s: %s [%s]\n", result == 77 ? "unavailable" : "failed",
                error.message == NULL ? "Incomplete observation" : error.message,
                error.code == NULL ? "incomplete" : error.code);
    tired_unit_query_destroy(query);
    tired_manager_probe_destroy(probe);
    tired_manager_identity_destroy(identity);
    sd_bus_close_unref(bus);
    return result;
}
