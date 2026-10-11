#include "m_double_tap.h"
#include <stddef.h>

bool xh_m_double_tap_short(xh_m_double_tap_t *state, uint32_t now_ms)
{
    if (state == NULL) return false;
    if (state->pending &&
        (uint32_t)(now_ms - state->first_up_ms) <= XH_M_DOUBLE_TAP_WINDOW_MS) {
        state->pending = false;
        return true;
    }
    /* MainTask polls BEFORE processing the next queued click; if an earlier
     * click expired, it already delivered the original single action. */
    state->first_up_ms = now_ms;
    state->pending = true;
    return false;
}

bool xh_m_double_tap_poll(xh_m_double_tap_t *state, uint32_t now_ms)
{
    if (state == NULL || !state->pending ||
        (uint32_t)(now_ms - state->first_up_ms) <= XH_M_DOUBLE_TAP_WINDOW_MS) {
        return false;
    }
    state->pending = false;
    return true;
}

void xh_m_double_tap_cancel(xh_m_double_tap_t *state)
{
    if (state != NULL) state->pending = false;
}
