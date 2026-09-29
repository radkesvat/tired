#include "tired/manager_job.h"
#include "tired/name.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
struct TiredManagerJob
{
    TiredManagerIdentity *identity;
    sd_bus *bus;
    sd_bus_slot *watch, *call;
    uint64_t deadline;
    char unit[209];
    TiredJobAction action;
    TiredJobEvent early[32];
    size_t count;
    TiredManagerJobResult result;
};
static bool now(uint64_t *value)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
        return false;
    *value = (uint64_t)time.tv_sec * 1000000 + (uint64_t)time.tv_nsec / 1000;
    return true;
}
static void fail(TiredManagerJob *job, TiredStatus status, const char *code, const char *message)
{
    job->result.done = true;
    tired_error_set(&job->result.error, status, code, message, 0);
}
static bool current(TiredManagerJob *job)
{
    if (job->result.done)
        return false;
    TiredManagerIdentityResult owner = tired_manager_identity_result(job->identity);
    if (!owner.ready || owner.error.status != TIRED_OK)
    {
        fail(job, TIRED_CONFLICT, "job-owner", "Manager identity changed while tracking a job.");
        if (owner.error.status != TIRED_OK)
            job->result.error = owner.error;
        return false;
    }
    uint64_t time;
    if (!now(&time))
        fail(job, TIRED_INTERNAL, "job-clock", "Cannot read job deadline clock.");
    else if (time >= job->deadline)
        fail(job, TIRED_RUNTIME_FAILED, "job-timeout",
             "Job tracking deadline expired; submitted work may still complete.");
    return !job->result.done;
}
static bool remote_error(TiredManagerJob *job, sd_bus_message *message)
{
    const sd_bus_error *error = sd_bus_message_get_error(message);
    if (error == NULL)
        return false;
    if (sd_bus_error_has_name(error, SD_BUS_ERROR_ACCESS_DENIED) ||
        sd_bus_error_has_name(error, SD_BUS_ERROR_AUTH_FAILED))
        fail(job, TIRED_AUTHORIZATION, "job-authorization",
             "Manager denied job tracking or submission.");
    else if (sd_bus_error_has_name(error, SD_BUS_ERROR_UNKNOWN_METHOD) ||
             sd_bus_error_has_name(error, SD_BUS_ERROR_UNKNOWN_INTERFACE))
        fail(job, TIRED_UNSUPPORTED, "job-unsupported", "Manager job operation is unavailable.");
    else
        fail(job, TIRED_RUNTIME_FAILED, "job-call",
             "Job request failed or its outcome is unknown.");
    return true;
}
static void finish(TiredManagerJob *job, const TiredJobEvent *event)
{
    job->result.completion = *event;
    job->result.finished = job->result.done = true;
    if (event->outcome != TIRED_JOB_DONE)
        tired_error_set(&job->result.error, TIRED_RUNTIME_FAILED, "job-result",
                        "Manager job did not complete successfully.", 0);
}
static int event(sd_bus_message *message, void *userdata, sd_bus_error *unused)
{
    (void)unused;
    TiredManagerJob *job = userdata;
    if (!current(job) || !job->result.submitted)
        return 1;
    /* Baseline sd-bus argument matching stops at a numeric argument, so arg2
     * cannot filter JobRemoved's uoss payload. Filter the selected unit here. */
    uint32_t id;
    const char *path = NULL, *unit = NULL;
    if (sd_bus_message_has_signature(message, "uoss") <= 0 ||
        sd_bus_message_read(message, "uos", &id, &path, &unit) <= 0 ||
        sd_bus_message_rewind(message, true) < 0)
    {
        fail(job, TIRED_INVALID, "job-signal", "Manager returned an invalid job signal.");
        return 1;
    }
    if (strcmp(unit, job->unit) != 0)
        return 1;
    TiredJobEvent observed = {0};
    TiredManagerIdentityResult owner = tired_manager_identity_result(job->identity);
    if (!tired_job_event_read(message, owner.unique_name, &observed, &job->result.error))
    {
        job->result.done = true;
        return 1;
    }
    if (strcmp(observed.unit, job->unit) != 0)
        return 1;
    if (job->result.accepted)
    {
        if (observed.id == job->result.job_id)
            finish(job, &observed);
    }
    else if (job->count == 32)
        fail(job, TIRED_RECOVERY_REQUIRED, "job-event-limit",
             "Too many early job completions to correlate safely.");
    else
        job->early[job->count++] = observed;
    return 1;
}
static int accepted(sd_bus_message *message, void *userdata, sd_bus_error *unused)
{
    (void)unused;
    TiredManagerJob *job = userdata;
    if (!current(job) || remote_error(job, message))
        return 1;
    const char *path = NULL;
    if (sd_bus_message_has_signature(message, "o") <= 0 ||
        sd_bus_message_read(message, "o", &path) <= 0 ||
        !tired_job_path_id(path, &job->result.job_id, &job->result.error))
    {
        fail(job, TIRED_INVALID, "job-reply", "Manager returned an invalid job object path.");
        return 1;
    }
    job->result.accepted = true;
    for (size_t i = 0; i < job->count; ++i)
        if (job->early[i].id == job->result.job_id)
        {
            finish(job, &job->early[i]);
            break;
        }
    return 1;
}
static bool queue(TiredManagerJob *job, const char *method, sd_bus_message_handler_t callback,
                  bool arguments)
{
    if (!current(job))
        return false;
    TiredManagerIdentityResult owner = tired_manager_identity_result(job->identity);
    uint64_t time;
    if (!now(&time) || time >= job->deadline)
    {
        fail(job, TIRED_RUNTIME_FAILED, "job-timeout", "Job deadline expired before submission.");
        return false;
    }
    sd_bus_message *message = NULL;
    int rc = sd_bus_message_new_method_call(job->bus, &message, owner.unique_name,
                                            "/org/freedesktop/systemd1",
                                            "org.freedesktop.systemd1.Manager", method);
    if (rc >= 0)
        rc = sd_bus_message_set_auto_start(message, 0);
    if (rc >= 0)
        rc = sd_bus_message_set_allow_interactive_authorization(message, 0);
    if (rc >= 0 && arguments)
        rc = sd_bus_message_append(message, "ss", job->unit, "replace");
    job->call = sd_bus_slot_unref(job->call);
    if (rc >= 0)
        rc = sd_bus_call_async(job->bus, &job->call, message, callback, job, job->deadline - time);
    sd_bus_message_unref(message);
    if (rc < 0)
    {
        fail(job, TIRED_RUNTIME_FAILED, "job-send", "Cannot queue manager job operation.");
        return false;
    }
    return true;
}
static int subscribed(sd_bus_message *message, void *userdata, sd_bus_error *unused)
{
    (void)unused;
    TiredManagerJob *job = userdata;
    if (!current(job))
        return 1;
    const sd_bus_error *error = sd_bus_message_get_error(message);
    bool existing =
        error != NULL && sd_bus_error_has_name(error, "org.freedesktop.systemd1.AlreadySubscribed");
    if (!existing && remote_error(job, message))
        return 1;
    if (!existing && sd_bus_message_has_signature(message, "") <= 0)
    {
        fail(job, TIRED_INVALID, "job-subscribe-reply",
             "Manager returned an invalid subscription reply.");
        return 1;
    }
    static const char *const methods[] = {"StartUnit", "StopUnit", "RestartUnit"};
    if (queue(job, methods[job->action], accepted, true))
        job->result.submitted = true;
    return 1;
}
static int installed(sd_bus_message *message, void *userdata, sd_bus_error *unused)
{
    (void)unused;
    TiredManagerJob *job = userdata;
    if (!current(job) || remote_error(job, message))
        return 1;
    if (sd_bus_message_has_signature(message, "") <= 0)
    {
        fail(job, TIRED_INVALID, "job-match-reply", "Bus returned an invalid signal match reply.");
        return 1;
    }
    (void)queue(job, "Subscribe", subscribed, false);
    return 1;
}
bool tired_manager_job_start(TiredManagerIdentity *identity, const char *unit,
                             TiredJobAction action, unsigned timeout_ms, TiredManagerJob **output,
                             TiredError *error)
{
    assert(identity != NULL && unit != NULL && output != NULL && *output == NULL);
    TiredManagerIdentityResult owner = tired_manager_identity_result(identity);
    size_t length = strnlen(unit, 209);
    if (!owner.ready || owner.error.status != TIRED_OK || (unsigned)action > TIRED_JOB_RESTART ||
        timeout_ms == 0 || timeout_ms > 300000 || length <= 8 || length > 208 ||
        memcmp(unit + length - 8, ".service", 8) != 0 ||
        !tired_name_validate_base(unit, length - 8, error))
        return tired_error_set(
            error, TIRED_INVALID, "job-input",
            "Job requires a ready identity, safe service name and bounded deadline.", 0);
    TiredManagerJob *job = calloc(1, sizeof(*job));
    if (job == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot allocate manager job.",
                               errno);
    job->identity = identity;
    job->bus = sd_bus_ref(tired_manager_identity_bus(identity));
    job->action = action;
    memcpy(job->unit, unit, length + 1);
    if (!now(&job->deadline))
    {
        tired_manager_job_destroy(job);
        return tired_error_set(error, TIRED_INTERNAL, "job-clock", "Cannot read job clock.", errno);
    }
    job->deadline += (uint64_t)timeout_ms * 1000;
    char match[512];
    int length_match =
        snprintf(match, sizeof(match),
                 "type='signal',sender='%s',path='/org/freedesktop/"
                 "systemd1',interface='org.freedesktop.systemd1.Manager',member='JobRemoved'",
                 owner.unique_name);
    int rc = length_match < 0 || (size_t)length_match >= sizeof(match)
                 ? -E2BIG
                 : sd_bus_add_match_async(job->bus, &job->watch, match, event, installed, job);
    if (rc < 0)
    {
        tired_manager_job_destroy(job);
        return tired_error_set(error, TIRED_RUNTIME_FAILED, "job-watch",
                               "Cannot subscribe to job completion events.", -rc);
    }
    *output = job;
    tired_error_clear(error);
    return true;
}
bool tired_manager_job_step(TiredManagerJob *job)
{
    assert(job != NULL);
    if (current(job))
    {
        (void)tired_manager_identity_step(job->identity);
        TiredManagerIdentityResult owner = tired_manager_identity_result(job->identity);
        if (owner.error.status != TIRED_OK)
        {
            job->result.done = true;
            job->result.error = owner.error;
        }
    }
    if (job->result.done)
    {
        job->call = sd_bus_slot_unref(job->call);
        job->watch = sd_bus_slot_unref(job->watch);
    }
    return job->result.done;
}
bool tired_manager_job_poll(TiredManagerJob *job, struct pollfd *descriptor,
                            uint64_t *deadline_usec, TiredError *error)
{
    assert(job != NULL && descriptor != NULL && deadline_usec != NULL);
    if (job->result.done)
    {
        *descriptor = (struct pollfd){.fd = -1};
        *deadline_usec = UINT64_MAX;
        tired_error_clear(error);
        return true;
    }
    if (!tired_manager_identity_poll(job->identity, descriptor, deadline_usec, error))
        return false;
    if (*deadline_usec > job->deadline)
        *deadline_usec = job->deadline;
    return true;
}
void tired_manager_job_cancel(TiredManagerJob *job)
{
    assert(job != NULL);
    if (!job->result.done)
        fail(job, TIRED_CANCELLED, "job-cancelled",
             "Job wait cancelled; submitted work may still complete.");
    job->call = sd_bus_slot_unref(job->call);
    job->watch = sd_bus_slot_unref(job->watch);
}
TiredManagerJobResult tired_manager_job_result(const TiredManagerJob *job)
{
    assert(job != NULL);
    TiredManagerJobResult result = job->result;
    TiredManagerIdentityResult owner = tired_manager_identity_result(job->identity);
    if (result.error.status == TIRED_OK && owner.error.status != TIRED_OK)
    {
        result.done = true;
        result.error = owner.error;
    }
    return result;
}
void tired_manager_job_destroy(TiredManagerJob *job)
{
    if (job == NULL)
        return;
    sd_bus_slot_unref(job->call);
    sd_bus_slot_unref(job->watch);
    sd_bus_unref(job->bus);
    free(job);
}
