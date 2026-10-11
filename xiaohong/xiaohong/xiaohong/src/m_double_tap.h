#ifndef XH_M_DOUBLE_TAP_H
#define XH_M_DOUBLE_TAP_H

#include <stdbool.h>
#include <stdint.h>

/* First-gen XiaoHong: recognize two M short-release events in MainTask,
 * NEVER within the GPIO ISR. The +/- hardware reset chord is unrelated.
 * The 500ms/5000ms M holds remain owned by key_config.c. */
#define XH_M_DOUBLE_TAP_WINDOW_MS 350U

typedef struct {
    bool pending;
    uint32_t first_up_ms;
} xh_m_double_tap_t;

/* Call once per received M short-release event; returns true for a double tap.
 * A single event is delayed until poll() returns true after the window. */
bool xh_m_double_tap_short(xh_m_double_tap_t *state, uint32_t now_ms);

/* Consume one expired single. Unsigned subtraction tolerates timer wrapping. */
bool xh_m_double_tap_poll(xh_m_double_tap_t *state, uint32_t now_ms);

void xh_m_double_tap_cancel(xh_m_double_tap_t *state);

#endif /* XH_M_DOUBLE_TAP_H */
