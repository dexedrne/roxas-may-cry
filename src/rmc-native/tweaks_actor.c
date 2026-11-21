#include "tweaks_game.h"
#include "look.h"

static void *event_orig, *actor_orig, *strike_orig, *touch_orig, *summon_orig;
unsigned rmc_forms[6];
void *rmc_form_match, *rmc_form_skip, *rmc_ability_match, *rmc_ability_skip;

/* Both mid-function sites continue with live registers and comparison flags. */
extern void rmc_form_branch(void), rmc_ability_branch(void);
__asm__(".text\n"
        "rmc_form_branch:\n"
        ".irp offset,0,4,8,12,16,20\n"
        "lea rmc_forms+\\offset(%rip),%rax\n"
        "cmp (%rax),%r8d\n"
        "je 1f\n"
        ".endr\n"
        "jmp *rmc_form_skip(%rip)\n"
        "1: jmp *rmc_form_match(%rip)\n"
        "rmc_ability_branch:\n"
        "cmp %dil,6(%rax)\n"
        "jne 2f\n"
        "cmp %di,(%rbx)\n"
        "jl 3f\n"
        "2: cmpw $1,12(%rax)\n"
        "jne 4f\n"
        "cmp %di,(%rbx)\n"
        "jl 3f\n"
        "4: jmp *rmc_ability_skip(%rip)\n"
        "3: jmp *rmc_ability_match(%rip)\n");

static u8 dual_form(u32 form) {
    for (unsigned i = 0; i < 6; i++)
        if (rmc_forms[i] == form)
            return 1;
    return 0;
}
static u32 dual_mask(void) {
    u32 result = 0;
    for (unsigned i = 0; i < 6; i++)
        if (rmc_forms[i] <= 6)
            result |= 1u << rmc_forms[i];
    return result;
}
static int ability_id(u16 encoded) { return (s16)(u16)(encoded << 1) >> 1; }
static void ability_list(u64 list) {
    u64 sheet = rd64(list + 0x28);
    u64 first = call4(0x4ad270, rd32(sheet + 0x250), 0, 0, 0);
    u64 second = call4(0x4ad270, rd32(sheet + 0x254), 0, 0, 0);
    for (int i = 0; first && i < 0x50; i++) {
        u16 e = rd16(first + 0x54 + 2 * i);
        if (!(e & 0x8000))
            continue;
        u32 id = (u32)ability_id(e);
        u64 info = call4(0x3e39a0, id, 0, 0, 0);
        if (rd16(info + 12) == 1 || !rd8(info + 6) || !second)
            call4(0x401730, list, id, 0, 0);
    }
    for (int i = 0; second && i < 0x18; i++) {
        u16 e = rd16(second + 8 + 2 * i);
        if ((e & 0x8000) && ability_id(e))
            call4(0x401730, list, (u32)ability_id(e), 0, 0);
    }
    static const unsigned first_bits[] = {2, 6, 10, 14, 0x16 * 8 + 5};
    for (unsigned i = 0; i < 5; i++) {
        for (unsigned k = 0; k < 4; k++) {
            unsigned bit = first_bits[i] + k;
            if (rd8(list + bit / 8) & (1u << (bit % 8))) {
                wr8(list + 0x20 + i, k + 1);
                break;
            }
        }
    }
}
typedef u64 (*Summon)(u64, u32, u64, float);
static u64 solo_summon(u64 actor, u32 kind, u64 pos, float distance) {
    u64 save = call4(0x152380, 0, 0, 0, 0);
    if (rd8(save + 0x3525))
        ((void (*)(u64, float))rmc_site(0x3ddf00))(pos, distance);
    return ((Summon)summon_orig)(actor, kind, pos, distance);
}
static void shopface(u32 id, char *out) {
    u32 selector = call4(0x3e3180, id, 0, 0, 0);
    u64 entry = call4(0x3dfeb0, selector, 0, 0, 0);
    char name[33];
    for (unsigned i = 0; i < 32; i++) {
        unsigned c = rd8(entry + 8 + i);
        name[i] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    name[32] = 0;
    snprintf(out, 260, "menu/shopface/%s.bin", name);
    look_shopface(out, 260);
}
static u64 map_event(u64 self, u64 ev, u64 arg3, u64 arg4) {
    s32 arg = (s16)rd16(ev + 4);
    switch (rd16(ev + 2)) {
    case 0x7f:
        if (arg >= 0x7d00)
            call4(0x1518e0, (u32)arg, 0, 0, 0);
        break;
    case 0x3e9: {
        u64 vec = rd64(call4(0x3b3e80, self, 0, 0, 0) + 0xdc8) + 4;
        call4(0x3bde30, vec, 0, 0, 0);
        u64 entry = call4(0x3d74e0, self, (u32)arg, 0, 0);
        if (entry)
            call4(0x3d78d0, self, entry, vec, 0);
        break;
    }
    case 0x3e8:
        if ((rd8(self + 0x18c) & 12) || (rd8(self + 0x18d) & 4) || (rd8(self + 0x194) & 0x20)) {
            u64 gate = call4(0x3ba9a0, self, 0, 0, 0);
            call4(0x4ad240, self, 0, 0, 0);
            if (arg) {
                if (gate)
                    ((void (*)(double, u64))rmc_site(0x409b00))(30.0, self);
                ((void (*)(u64, u32, float, float))rmc_site(0x3c6dc0))(self + 0x158, arg + !!gate, 0, 0);
                ((void (*)(u64, u32, float, float, u32))rmc_site(0x3c8310))(self + 0x158, gate ? 4 : 0, 0, 0,
                                                                            0);
            }
            if (rd8(ev + 0x2f) == 1)
                call4(0x3b5fa0, self, self, (u32)(s32)(s16)rd16(ev), 0);
        }
        break;
    }
    return ((Fn4)event_orig)(self, ev, arg3, arg4);
}
static u64 actor_init(u64 a, u64 b, u64 c, u64 d) {
    call4(0x3b4f80, a, b, c, d);
    u64 script = call4(0x3e1730, rd64(data_at(0x2a0f688)), 0, 0, 0);
    call5(0x3e1840, script, 0x65, 0, 0, 0);
    call4(0x3e1410, script, 0, 0, 0);
    call4(0x3e1510, script, 0, 0, 0);
    return ((Fn4)actor_orig)(a, b, c, d);
}
static u64 actor_strike(u64 a, u64 b, u64 c, u64 d) {
    u64 refs[2] = {rd64(b), rd64(c)}, slots[4];
    for (unsigned i = 0; i < 2; i++) {
        slots[2 * i] = (u32)call4(0x4ad240, refs[i], 0, 0, 0);
        u64 who = call4(0x3eafc0, rd32(refs[i] + 0x18), rd32(refs[i] + 0x1c), 0, 0);
        slots[2 * i + 1] = (u32)call4(0x4ad240, who, 0, 0, 0);
    }
    u64 script = rd64(a + 0x5b0);
    if (script)
        call5(0x3e1840, script, 0x13, slots, 4, 0);
    return ((Fn4)strike_orig)(a, b, c, d);
}
static u64 actor_touch(u64 a, u64 b, u64 c, u64 d) {
    if (rd64(a + 0x5b0)) {
        u64 slot = (u32)call4(0x4ad240, b, 0, 0, 0);
        call5(0x3e1840, rd64(a + 0x5b0), 6, &slot, 1, 0);
    }
    return ((Fn4)touch_orig)(a, b, c, d);
}
void tweaks_actor_plan(void) {
    for (unsigned i = 0; i < 6; i++)
        rmc_forms[i] = (u32)rmc_value("forms", "dualwield_forms", i);
    rmc_form_match = rmc_site(0x3c40bf);
    rmc_form_skip = rmc_site(0x3c41a5);
    rmc_hook(0x3c40a9, rmc_form_branch, NULL);
    rmc_hook(0x2fc860, dual_form, NULL);
    rmc_hook(0x2fc4e0, dual_mask, NULL);
    if (rmc_int("combat", "patch_ability_categories")) {
        rmc_ability_match = rmc_site(0x360a6d);
        rmc_ability_skip = rmc_site(0x360ba5);
        rmc_hook(0x360a5a, rmc_ability_branch, NULL);
        rmc_hook(0x4014a0, ability_list, NULL);
    }
    if (rmc_int("forms", "solo_summon_fix"))
        rmc_hook(0x3a7cb0, solo_summon, &summon_orig);
    if (rmc_int("shop", "shopface_fix"))
        rmc_hook(0x29efa0, shopface, NULL);
    rmc_hook(0x405440, map_event, &event_orig);
    rmc_hook(0x3a89a0, actor_init, &actor_orig);
    rmc_hook(0x3d61d0, actor_strike, &strike_orig);
    rmc_hook(0x3d60c0, actor_touch, &touch_orig);
}
