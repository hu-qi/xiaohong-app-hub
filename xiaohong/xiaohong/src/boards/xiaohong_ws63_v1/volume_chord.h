/* First-generation XiaoHong: two-volume-key overlap recognition.
 * Pure C logic; ISR caller owns serialization, no heap/OS/LVGL dependencies.
 */
#ifndef XH_VOLUME_CHORD_H
#define XH_VOLUME_CHORD_H
#include <stdbool.h>
#include <stdint.h>
typedef enum {
    XH_VOLUME_PLUS = 0,
    XH_VOLUME_MINUS = 1
} xh_volume_button_t;
typedef enum {
    XH_VOLUME_EVENT_NONE = 0,
    XH_VOLUME_EVENT_PLUS_SHORT,
    XH_VOLUME_EVENT_PLUS_LONG,
    XH_VOLUME_EVENT_MINUS_SHORT,
    XH_VOLUME_EVENT_MINUS_LONG,
    XH_VOLUME_EVENT_HUB_TOGGLE
} xh_volume_event_t;
typedef struct {
    bool down[2];
    bool chord_consumed;
    uint32_t down_since[2];
    uint32_t overlap_since;
} xh_volume_chord_t;
/* One edge at a time. Held chord fires once on the first release, after
 * both volume buttons have overlapped for at least chord_ms.
 * Time subtraction handles uint32_t wraparound.
 */
xh_volume_event_t xh_volume_chord_edge(xh_volume_chord_t *state,
                                       xh_volume_button_t button,
                                       bool pressed,
                                       uint32_t now_ms,
                                       uint32_t long_ms,
                                       uint32_t chord_ms);
#endif
