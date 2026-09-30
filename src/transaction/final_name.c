#include "tired/collision.h"
#include "tired/file_target.h"
#include "tired/io.h"
#include "tired/mutation.h"
#include "tired/name.h"
#include "tired/render.h"
#include <string.h>
bool tired_mutation_final_name(TiredMutation *mutation, const TiredLayout *layout,
                               const TiredBackend *backend, TiredError *error)
{
    TiredServiceRecord *record = &mutation->proposed;
    TiredText base = {0}, candidate = {0}, unit = {0};
    TiredNameBasis basis;
    bool ok = false;
    if (!tired_name_suggest(&record->spec.fields[TIRED_FIELD_ARGV].value.list, &base, &basis,
                            error))
        goto done;
    uint64_t deadline = tired_monotonic_usec() + 5000000;
    for (uint64_t ordinal = 1; tired_monotonic_usec() < deadline; ++ordinal)
    {
        TiredRuntime runtime = {0};
        TiredCollision collision = {0};
        TiredTextList pending = {0};
        bool queried = tired_name_candidate(&base, ordinal, &candidate, error) &&
                       backend->query(backend->context, candidate.data, false, &runtime, error);
        TiredUnitQueryResult observation = {.done = queried,
                                            .file_found = runtime.found,
                                            .object_found = runtime.loaded,
                                            .unit_name = candidate.data};
        bool checked =
            queried && tired_collision_check(&candidate, &observation, backend->load_paths,
                                             &pending, &collision, error);
        bool clear = checked && collision.kind == TIRED_COLLISION_NONE;
        tired_runtime_destroy(&runtime);
        tired_collision_destroy(&collision);
        if (!checked)
            goto done;
        if (!clear)
            continue;
        ok = tired_spec_set(&record->spec, TIRED_FIELD_NAME, candidate.data, candidate.length - 8,
                            TIRED_ORIGIN_CAPTURE, true, error);
        if (ok && !record->spec.fields[TIRED_FIELD_SYSLOG_IDENTIFIER].inherit &&
            (record->spec.fields[TIRED_FIELD_SYSLOG_IDENTIFIER].origin == TIRED_ORIGIN_CAPTURE ||
             record->spec.fields[TIRED_FIELD_SYSLOG_IDENTIFIER].origin == TIRED_ORIGIN_DEFAULT))
            ok = tired_spec_set(&record->spec, TIRED_FIELD_SYSLOG_IDENTIFIER, candidate.data,
                                candidate.length - 8, TIRED_ORIGIN_CAPTURE, true, error);
        TiredFileTarget target = {.role = TIRED_FILE_TARGET_UNIT, .unit_name = candidate};
        memcpy(target.service_uuid, record->metadata.service_uuid, 37);
        TiredResolvedFile resolved = {0};
        ok = ok &&
             tired_text_set(&record->metadata.unit_name, candidate.data, candidate.length, 255,
                            error) &&
             tired_file_target_resolve(layout, &target, &resolved, error) &&
             tired_path_absolute(&resolved.directory, resolved.name.data, resolved.name.length,
                                 &record->unit_path, error) &&
             tired_render_unit(&record->spec, record->metadata.service_uuid,
                               record->has_environment ? &record->environment_path : NULL,
                               &record->credentials, &unit, error) &&
             tired_digest_bytes(unit.data, unit.length, record->metadata.unit_sha256, error) &&
             tired_mutation_digest(mutation, record->review.approved_sha256, error);
        tired_resolved_file_destroy(&resolved);
        break;
    }
done:
    tired_text_destroy(&base);
    tired_text_destroy(&candidate);
    tired_text_destroy(&unit);
    if (!ok && error->status == TIRED_OK)
        tired_error_set(error, TIRED_CONFLICT, "name-selection-timeout",
                        "Final name selection exceeded its deadline.", 0);
    return ok;
}
