#include "../../src/cli/inspection_live.h"
#include "tired/linger_observation.h"
#include "tired/linger_query.h"
#include "tired/load_paths.h"
#include "tired/manager.h"
#include "tired/name_query.h"
#include "tired/observed_file.h"
#include "tired/unit_batch.h"
#include "tired/unit_query.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static void tick(void)
{
    struct timespec delay = {.tv_nsec = 1000000};
    (void)nanosleep(&delay, NULL);
}
int main(void)
{
    sd_bus *bus = NULL;
    TiredManagerIdentity *login = NULL;
    TiredLingerQuery *linger = NULL;
    TiredManagerIdentity *identity = NULL;
    TiredManagerProbe *probe = NULL;
    TiredUnitQuery *query = NULL;
    TiredLoadPaths *paths = NULL;
    TiredNameQuery *names = NULL;
    TiredUnitBatch *batch = NULL;
    TiredInspectionLive cli_live = {0};
    TiredInspectionLive configuration = {0};
    TiredObservedFile fragment = {0};
    struct json_object *live_json = NULL;
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
    TiredText full_name = {.data = "systemd-journald.service", .length = 24};
    TiredTextList batch_names = {.items = &full_name, .count = 1};
    if (!tired_unit_batch_start(identity, &batch_names, 3000, &batch, &error))
        goto done;
    for (unsigned i = 0; i < 5000 && !tired_unit_batch_step(batch); ++i)
        tick();
    TiredUnitBatchResult batch_result = tired_unit_batch_result(batch);
    error = batch_result.error;
    if (!batch_result.done || error.status != TIRED_OK)
        goto done;
    TiredUnitBatchItem item = tired_unit_batch_item(batch, 0);
    error = item.query.error;
    if (!item.query.done || error.status != TIRED_OK || item.completed_realtime_usec == 0)
        goto done;
    tired_inspection_live_collect(false, &batch_names, &cli_live);
    error = cli_live.error;
    if (error.status != TIRED_OK)
        goto done;
    TiredUnitBatchItem cli_item = tired_inspection_live_item(&cli_live, 0);
    error = cli_item.query.error;
    if (!cli_item.query.done || error.status != TIRED_OK)
        goto done;
    live_json = tired_inspection_live_json(&cli_item);
    struct json_object *live_status = NULL;
    if (live_json == NULL || !json_object_object_get_ex(live_json, "status", &live_status) ||
        strcmp(json_object_get_string(live_status), "observed") != 0)
        goto done;
    if (observed.object_found)
    {
        tired_inspection_configuration_collect(false, &full_name, &configuration);
        TiredUnitBatchItem loaded = tired_inspection_live_item(&configuration, 0);
        error = loaded.query.error;
        if (!loaded.query.done || error.status != TIRED_OK ||
            !loaded.query.configuration_load_queued ||
            !loaded.query.configuration_load_acknowledged || loaded.completed_realtime_usec == 0 ||
            loaded.query.observation == NULL)
            goto done;
        const TiredObservedValue *path = &loaded.query.observation->fields[TIRED_OBS_FRAGMENT_PATH];
        if (path->known && path->value.text.length != 0)
        {
            size_t budget = 4U * TIRED_INPUT_LIMIT;
            if (!tired_observed_file_read(path->value.text.data, false, &budget, &fragment,
                                          &error) ||
                !fragment.fingerprint.exists)
                goto done;
            printf("Loaded configuration inspected; fragment bytes: %zu\n", fragment.bytes.length);
        }
    }
    if (observed.file_found || observed.object_found)
    {
        TiredText destination = {.data = "/etc/systemd/system", .length = 19};
        TiredTextList pending = {0};
        if (!tired_name_query_start(identity, &name, true, &destination, &pending, 3000, &names,
                                    &error))
            goto done;
        for (unsigned i = 0; i < 5000 && !tired_name_query_step(names); ++i)
            tick();
        TiredNameQueryResult selected = tired_name_query_result(names);
        error = selected.error;
        if (!selected.done || error.status != TIRED_CONFLICT || selected.unit_name != NULL)
            goto done;
        printf("Existing explicit name rejected by live name discovery.\n");
    }
    if (!tired_login_identity_start(bus, 3000, &login, &error))
        goto done;
    for (unsigned i = 0; i < 5000 && !tired_manager_identity_step(login); ++i)
        tick();
    TiredManagerIdentityResult login_owner = tired_manager_identity_result(login);
    error = login_owner.error;
    if (login_owner.ready)
    {
        if (!tired_linger_query_start(login, getuid(), 3000, &linger, &error))
            goto done;
        for (unsigned i = 0; i < 5000 && !tired_linger_query_step(linger); ++i)
            tick();
        TiredLingerResult account = tired_linger_query_result(linger);
        error = account.error;
        if (!account.done || account.uid != getuid() || account.known != (error.status == TIRED_OK))
            goto done;
        if (account.known)
            printf("Current account lingering observed: %s\n",
                   account.enabled ? "enabled" : "disabled");
        else if (error.status == TIRED_NOT_FOUND || error.status == TIRED_UNSUPPORTED ||
                 error.status == TIRED_AUTHORIZATION)
            printf("Current account lingering unavailable: %s\n", error.code);
        else
            goto done;
    }
    else if (error.status == TIRED_NOT_FOUND || error.status == TIRED_AUTHORIZATION)
        printf("Login-manager observation unavailable: %s\n", error.code);
    else
        goto done;
    TiredLingerObservation account_observation;
    tired_linger_observe(2000, &account_observation);
    error = account_observation.result.error;
    if (!account_observation.attempted || !account_observation.result.done ||
        account_observation.result.uid != getuid())
        goto done;
    if (account_observation.result.known)
    {
        if (error.status != TIRED_OK || account_observation.completed_realtime_usec == 0)
            goto done;
        printf("Frontend account observation completed.\n");
    }
    else if (error.status != TIRED_NOT_FOUND && error.status != TIRED_UNSUPPORTED &&
             error.status != TIRED_AUTHORIZATION)
        goto done;
    result = 0;
done:
    tired_linger_query_destroy(linger);
    tired_manager_identity_destroy(login);
    tired_observed_file_destroy(&fragment);
    tired_inspection_live_destroy(&configuration);
    json_object_put(live_json);
    tired_inspection_live_destroy(&cli_live);
    tired_unit_batch_destroy(batch);
    tired_name_query_destroy(names);
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
