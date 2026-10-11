#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "m_double_tap.h"

static void single_tap(void)
{
    xh_m_double_tap_t m = {0};
    assert(!xh_m_double_tap_short(&m, 100));
    assert(!xh_m_double_tap_poll(&m, 450));
    assert(xh_m_double_tap_poll(&m, 451));
    assert(!xh_m_double_tap_poll(&m, 900));
}
static void double_tap(void)
{
    xh_m_double_tap_t m = {0};
    assert(!xh_m_double_tap_short(&m, 100));
    assert(xh_m_double_tap_short(&m, 450)); /* inclusive boundary */
    assert(!xh_m_double_tap_poll(&m, 1000));
    assert(!m.pending);
}
static void separated_taps(void)
{
    xh_m_double_tap_t m = {0};
    assert(!xh_m_double_tap_short(&m, 100));
    assert(xh_m_double_tap_poll(&m, 451));
    assert(!xh_m_double_tap_short(&m, 460));
    assert(xh_m_double_tap_poll(&m, 811));
}
static void triple_tap(void)
{
    xh_m_double_tap_t m = {0};
    assert(!xh_m_double_tap_short(&m, 100));
    assert(xh_m_double_tap_short(&m, 250));
    assert(!xh_m_double_tap_short(&m, 300));
    assert(xh_m_double_tap_poll(&m, 651));
}
static void cancel_on_long(void)
{
    xh_m_double_tap_t m = {0};
    assert(!xh_m_double_tap_short(&m, 100));
    xh_m_double_tap_cancel(&m);
    assert(!xh_m_double_tap_poll(&m, 10000));
    assert(!xh_m_double_tap_short(&m, 10100));
    assert(xh_m_double_tap_short(&m, 10150));
}
static void wraparound(void)
{
    xh_m_double_tap_t m = {0};
    assert(!xh_m_double_tap_short(&m, UINT32_MAX - 120));
    assert(xh_m_double_tap_short(&m, 80));
    assert(!xh_m_double_tap_short(&m, UINT32_MAX - 10));
    assert(xh_m_double_tap_poll(&m, 400));
}
static void invalid(void)
{
    assert(!xh_m_double_tap_short(NULL, 100));
    assert(!xh_m_double_tap_poll(NULL, 100));
    xh_m_double_tap_cancel(NULL);
}
int main(void)
{
    single_tap(); double_tap(); separated_taps();
    triple_tap(); cancel_on_long(); wraparound(); invalid();
    puts("PASS: M single delay / 350ms double / long cancel / wraparound");
    return 0;
}
