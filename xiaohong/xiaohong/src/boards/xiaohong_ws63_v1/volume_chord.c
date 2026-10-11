#include "volume_chord.h"
#include <stddef.h>

xh_volume_event_t xh_volume_chord_edge(xh_volume_chord_t *state,
                                       xh_volume_button_t button,
                                       bool pressed,
                                       uint32_t now_ms,
                                       uint32_t long_ms,
                                       uint32_t chord_ms)
{
    if (state == NULL || (unsigned)button > 1U) {
        return XH_VOLUME_EVENT_NONE;
    }
    const unsigned idx = (unsigned)button;
    const unsigned other = idx ^ 1U;
    if (state->down[idx] == pressed) {
        return XH_VOLUME_EVENT_NONE; /* redundant/bouncing same-state edge */
    }

    if (pressed) {
        state->down[idx] = true;
        state->down_since[idx] = now_ms;
        if (state->down[other] && !state->chord_consumed) {
            state->overlap_since = now_ms;
        }
        return XH_VOLUME_EVENT_NONE;
    }

    /* Determine a chord BEFORE changing state, while both are logically down. */
    const bool chord = !state->chord_consumed && state->down[other] &&
        (uint32_t)(now_ms - state->overlap_since) >= chord_ms;
    const bool was_consumed = state->chord_consumed;
    const uint32_t held_ms = (uint32_t)(now_ms - state->down_since[idx]);
    state->down[idx] = false;
    if (chord) {
        state->chord_consumed = true;
        return XH_VOLUME_EVENT_HUB_TOGGLE;
    }
    if (was_consumed) {
        if (!state->down[other]) {
            state->chord_consumed = false;
        }
        return XH_VOLUME_EVENT_NONE;
    }

    if (idx == XH_VOLUME_PLUS) {
        return held_ms >= long_ms ?
            XH_VOLUME_EVENT_PLUS_LONG : XH_VOLUME_EVENT_PLUS_SHORT;
    }
    return held_ms >= long_ms ?
        XH_VOLUME_EVENT_MINUS_LONG : XH_VOLUME_EVENT_MINUS_SHORT;
}
