/* Test the REAL, not a mocked, first-gen XiaoHong app registry. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "app_registry.h"

#define MOCK_GAME(game) \
    bool game##_create(lv_obj_t *parent) { return parent != NULL; } \
    void game##_destroy(void) {} \
    void game##_on_key(uint8_t key) { (void)key; } \
    void game##_on_tick(void) {}
MOCK_GAME(snake_game)
MOCK_GAME(tetris_game)
MOCK_GAME(breakout_game)
MOCK_GAME(liquid_duck)

int main(void) {
    uint8_t count = 0;
    const xh_app_entry_t *all = xh_app_registry_get_all(&count);
    assert(all && count == 5);
    const xh_app_entry_t *a = xh_app_registry_find_by_id(XH_APP_ID_ASSISTANT);
    assert(a && a == &all[0]);
    assert(a->available && a->system_app && a->default_app);
    assert(a->on_create == NULL && a->on_destroy == NULL);
    assert(a->on_tick == NULL && a->on_key == NULL);
    assert(xh_app_registry_find_by_id(XH_APP_ID_SNAKE)->available);
    assert(!xh_app_registry_find_by_id(XH_APP_ID_TETRIS)->available);
    unsigned defaults = 0, sysapps = 0;
    for (uint8_t i = 0; i < count; ++i) {
        defaults += all[i].default_app ? 1U : 0U;
        sysapps += all[i].system_app ? 1U : 0U;
        if (all[i].default_app) assert(all[i].system_app && all[i].available);
    }
    assert(defaults == 1 && sysapps == 1);
    assert(xh_app_registry_find_by_id(255) == NULL);
    puts("PASS: actual registry holds exactly one non-restartable, built-in default Assistant");
    return 0;
}
