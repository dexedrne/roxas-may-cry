/* Host tests execute native C against synthetic records. */
#include "tweaks_game.h"
#include <assert.h>

static u8 layout_name[32];
static u8 (*predicate)(u32);
static u32 (*mask)(void), (*header_icon)(void);
typedef u64 (*Motion)(u64, u64, float);
static Motion range_hook, frame_hook;
static unsigned forwards;
static u64 motion_forward(u64 record, u64 context, float bound) {
    assert(record && context && bound == 0.25f);
    return ++forwards;
}
unsigned char *rmc_site(unsigned id) {
    if (id == 0x9babb0)
        return layout_name;
    assert(id == 0x3c40bf || id == 0x3c41a5 || id == 0x360a6d || id == 0x360ba5);
    return NULL;
}
void rmc_hook(unsigned id, void *fn, void **original) {
    switch (id) {
    case 0x2fc860:
        predicate = fn;
        break;
    case 0x2fc4e0:
        mask = fn;
        break;
    case 0x153850:
        header_icon = fn;
        break;
    case 0x40d810:
        range_hook = fn;
        *original = (void *)motion_forward;
        break;
    case 0x40dd70:
        frame_hook = fn;
        *original = (void *)motion_forward;
        break;
    }
}
void tweaks_actor_plan(void), tweaks_menu_plan(void), tweaks_motion_plan(void);
int main(void) {
    char error[256];
    assert(rmc_config_load(NULL, error, sizeof error));
    tweaks_actor_plan();
    tweaks_menu_plan();
    tweaks_motion_plan();
    assert(mask() == 0x2d);
    for (unsigned i = 0; i < 32; i++)
        assert(predicate(i) == !!((0x2du >> i) & 1));
    strcpy((char *)layout_name, "bb0command.2dd");
    assert(header_icon() == (u32)(s32)(s8)0xec);
    strcpy((char *)layout_name, "nomatch");
    assert(header_icon() == (u32)(s32)(s8)0xe2);

    u8 record[16] = {0}, context[0x158] = {0}, actor[0x800] = {0}, sheet[0x200] = {0};
    wr64((u64)context + 0x10, (u64)actor);
    wr64((u64)actor + 0x5c0, (u64)sheet);
    wr32((u64)sheet + 8, 123);
    record[2] = 10;
    record[4] = 64;
    wr16((u64)record + 6, 0xfffe);
    assert(range_hook((u64)record, (u64)context, 0.25f) == 1 && (s32)rd32((u64)sheet + 8) == -2);
    wr32((u64)context + 0x44, 0x41200000); /* frame 10 restores the saved minimum */
    assert(range_hook((u64)record, (u64)context, 0.25f) == 2 && rd32((u64)sheet + 8) == 123);
    record[2] = 52;
    assert(frame_hook((u64)record, (u64)context, 0.25f) == 3 && rd32((u64)actor + 0x134) == 0x42c60000);
    puts("dual-wield forms, layout icons, motion override/restore and frame dispatch passed");
    return 0;
}

void campaign_command_done(uint64_t controller,uint64_t actor) { (void)controller; (void)actor; }

void look_shopface(char *path, size_t capacity) { (void)path; (void)capacity; }
