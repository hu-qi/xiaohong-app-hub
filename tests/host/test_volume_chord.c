/* Pure recognizer regression suite for the XiaoHong volume-key combo. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "volume_chord.h"
#define HOLD 500U
#define CHORD 1000U
static xh_volume_chord_t state;
static xh_volume_event_t edge(xh_volume_button_t button,bool press,uint32_t ms) {
    return xh_volume_chord_edge(&state,button,press,ms,HOLD,CHORD);
}
static void reset(void) { memset(&state,0,sizeof(state)); }
int main(void) {
    reset();
    /* A solo short or long is preserved. */
    assert(edge(XH_VOLUME_PLUS,true,100)==XH_VOLUME_EVENT_NONE);
    assert(edge(XH_VOLUME_PLUS,false,240)==XH_VOLUME_EVENT_PLUS_SHORT);
    assert(edge(XH_VOLUME_MINUS,true,300)==XH_VOLUME_EVENT_NONE);
    assert(edge(XH_VOLUME_MINUS,false,850)==XH_VOLUME_EVENT_MINUS_LONG);
    assert(edge(XH_VOLUME_PLUS,true,1100)==XH_VOLUME_EVENT_NONE);
    assert(edge(XH_VOLUME_PLUS,false,1600)==XH_VOLUME_EVENT_PLUS_LONG);
    assert(edge(XH_VOLUME_MINUS,true,2000)==XH_VOLUME_EVENT_NONE);
    assert(edge(XH_VOLUME_MINUS,false,2499)==XH_VOLUME_EVENT_MINUS_SHORT);

    reset();
    /* +/- hold >1000ms overlap, releasing PLUS first: a single toggle. */
    assert(edge(XH_VOLUME_PLUS,true,1000)==XH_VOLUME_EVENT_NONE);
    assert(edge(XH_VOLUME_MINUS,true,1100)==XH_VOLUME_EVENT_NONE);
    assert(edge(XH_VOLUME_PLUS,false,2100)==XH_VOLUME_EVENT_HUB_TOGGLE);
    assert(edge(XH_VOLUME_MINUS,false,2150)==XH_VOLUME_EVENT_NONE);
    assert(edge(XH_VOLUME_MINUS,false,2200)==XH_VOLUME_EVENT_NONE);
    assert(!state.chord_consumed);

    reset();
    /* Reverse press/release order. */
    assert(edge(XH_VOLUME_MINUS,true,100)==XH_VOLUME_EVENT_NONE);
    assert(edge(XH_VOLUME_PLUS,true,200)==XH_VOLUME_EVENT_NONE);
    assert(edge(XH_VOLUME_MINUS,false,1201)==XH_VOLUME_EVENT_HUB_TOGGLE);
    assert(edge(XH_VOLUME_PLUS,false,1210)==XH_VOLUME_EVENT_NONE);

    reset();
    /* Short overlap never opens the App Hub, even if first key was long. */
    assert(edge(XH_VOLUME_PLUS,true,100)==XH_VOLUME_EVENT_NONE);
    assert(edge(XH_VOLUME_MINUS,true,1100)==XH_VOLUME_EVENT_NONE);
    assert(edge(XH_VOLUME_MINUS,false,1220)==XH_VOLUME_EVENT_MINUS_SHORT);
    assert(edge(XH_VOLUME_PLUS,false,1300)==XH_VOLUME_EVENT_PLUS_LONG);

    reset();
    /* Both 550ms long (500ms threshold) does NOT count as a chord. */
    assert(edge(XH_VOLUME_PLUS,true,100)==XH_VOLUME_EVENT_NONE);
    assert(edge(XH_VOLUME_MINUS,true,130)==XH_VOLUME_EVENT_NONE);
    assert(edge(XH_VOLUME_PLUS,false,700)==XH_VOLUME_EVENT_PLUS_LONG);
    assert(edge(XH_VOLUME_MINUS,false,740)==XH_VOLUME_EVENT_MINUS_LONG);

    reset();
    /* Only first release determines a valid chord; no second key event. */
    assert(edge(XH_VOLUME_MINUS,true,0)==XH_VOLUME_EVENT_NONE);
    assert(edge(XH_VOLUME_PLUS,true,10)==XH_VOLUME_EVENT_NONE);
    assert(edge(XH_VOLUME_PLUS,false,1050)==XH_VOLUME_EVENT_HUB_TOGGLE);
    assert(edge(XH_VOLUME_PLUS,true,1060)==XH_VOLUME_EVENT_NONE);
    assert(edge(XH_VOLUME_PLUS,false,1100)==XH_VOLUME_EVENT_NONE);
    assert(edge(XH_VOLUME_MINUS,false,1200)==XH_VOLUME_EVENT_NONE);

    reset();
    /* The 32-bit monotonic millisecond counter can wrap during the gesture. */
    assert(edge(XH_VOLUME_PLUS,true,UINT32_MAX-700)==XH_VOLUME_EVENT_NONE);
    assert(edge(XH_VOLUME_MINUS,true,UINT32_MAX-100)==XH_VOLUME_EVENT_NONE);
    assert(edge(XH_VOLUME_MINUS,false,1000)==XH_VOLUME_EVENT_HUB_TOGGLE);
    assert(edge(XH_VOLUME_PLUS,false,1050)==XH_VOLUME_EVENT_NONE);

    reset();
    /* A second independent chord is allowed after all keys were released. */
    for (uint32_t n=0;n<4;n++) {
        uint32_t t=2000+n*3000;
        assert(edge(XH_VOLUME_PLUS,true,t)==XH_VOLUME_EVENT_NONE);
        assert(edge(XH_VOLUME_MINUS,true,t+100)==XH_VOLUME_EVENT_NONE);
        assert(edge(XH_VOLUME_MINUS,false,t+1250)==XH_VOLUME_EVENT_HUB_TOGGLE);
        assert(edge(XH_VOLUME_PLUS,false,t+1260)==XH_VOLUME_EVENT_NONE);
    }

    puts("PASS: solo +/- brightness, threshold, one-shot dual hold, overlap, rollover, repeated chords");
    return 0;
}
