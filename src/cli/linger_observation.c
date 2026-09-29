#include "tired/linger_observation.h"
#include "tired/manager.h"
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
static bool clock_usec(clockid_t id, uint64_t *value, TiredError *error)
{
    struct timespec time;
    if (clock_gettime(id, &time) != 0)
        return tired_error_set(error, TIRED_INTERNAL, "linger-observation-clock",
                               "Cannot read lingering observation clock.", errno);
    *value = (uint64_t)time.tv_sec * 1000000U + (uint64_t)time.tv_nsec / 1000U;
    return true;
}
static bool remaining(uint64_t deadline, unsigned *ms, TiredError *error)
{
    uint64_t time;
    if (!clock_usec(CLOCK_MONOTONIC, &time, error))
        return false;
    if (time >= deadline)
        return tired_error_set(error, TIRED_RUNTIME_FAILED, "linger-observation-timeout",
                               "Account lingering observation timed out.", 0);
    *ms = (unsigned)((deadline - time + 999U) / 1000U);
    return true;
}
void tired_linger_observe(unsigned timeout_ms, TiredLingerObservation *output)
{
    assert(output != NULL);
    TiredLingerObservation observed = {.attempted = true, .result.uid = getuid()};
    TiredError *error = &observed.result.error;
    sd_bus *bus = NULL;
    TiredManagerIdentity *identity = NULL;
    TiredLingerQuery *query = NULL;
    uint64_t deadline;
    unsigned left;
    if (timeout_ms == 0 || timeout_ms > 300000)
    {
        tired_error_set(error, TIRED_INVALID, "linger-observation-input",
                        "Expected a bounded account observation deadline.", 0);
        goto done;
    }
    if (getuid() != geteuid() || getgid() != getegid())
    {
        tired_error_set(error, TIRED_AUTHORIZATION, "linger-observation-identity",
                        "Account observation requires matching real and effective identities.", 0);
        goto done;
    }
    if (!clock_usec(CLOCK_MONOTONIC, &deadline, error))
        goto done;
    deadline += (uint64_t)timeout_ms * 1000U;
    if (!tired_manager_bus_open(false, &bus, error) || !remaining(deadline, &left, error) ||
        !tired_login_identity_start(bus, left, &identity, error))
        goto done;
    for (;;)
    {
        if (!remaining(deadline, &left, error))
            goto done;
        if (query == NULL)
        {
            if (tired_manager_identity_step(identity))
            {
                TiredManagerIdentityResult owner = tired_manager_identity_result(identity);
                if (!owner.ready)
                {
                    *error = owner.error;
                    goto done;
                }
                if (!remaining(deadline, &left, error) ||
                    !tired_linger_query_start(identity, observed.result.uid, left, &query, error))
                    goto done;
                continue;
            }
        }
        else if (tired_linger_query_step(query))
        {
            observed.result = tired_linger_query_result(query);
            if (observed.result.error.status == TIRED_OK)
                (void)clock_usec(CLOCK_REALTIME, &observed.completed_realtime_usec, error);
            goto done;
        }
        struct pollfd descriptor;
        uint64_t native_deadline, time;
        bool ready =
            query == NULL
                ? tired_manager_identity_poll(identity, &descriptor, &native_deadline, error)
                : tired_linger_query_poll(query, &descriptor, &native_deadline, error);
        if (!ready || !clock_usec(CLOCK_MONOTONIC, &time, error))
            goto done;
        if (native_deadline > deadline)
            native_deadline = deadline;
        uint64_t wait = time >= native_deadline ? 0 : (native_deadline - time + 999U) / 1000U;
        int rc = poll(&descriptor, 1, wait > 100 ? 100 : (int)wait);
        if (rc < 0 && errno != EINTR)
        {
            tired_error_set(error, TIRED_RUNTIME_FAILED, "linger-observation-poll",
                            "Cannot wait for account observation.", errno);
            goto done;
        }
    }
done:
    observed.result.done = true;
    if (observed.result.error.status != TIRED_OK)
    {
        observed.result.known = observed.result.enabled = false;
        observed.completed_realtime_usec = 0;
    }
    tired_linger_query_destroy(query);
    tired_manager_identity_destroy(identity);
    sd_bus_close_unref(bus);
    *output = observed;
}
