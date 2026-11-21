#include "config.h"
#include "campaign.h"
#include <string.h>
#include "rmc.h"
#include <stdatomic.h>

typedef uintptr_t (*GameFn)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
static GameFn hp_original, mp_original, award_original, display_original;
static void (*recharge_original)(uintptr_t,float);
static uintptr_t *player;
static _Atomic unsigned exp_mult, infinite_hp, infinite_mp, infinite_drive;

static int active_sheet(uintptr_t sheet) {
    return *player && *(uintptr_t *)(*player + 0x5c0) == sheet;
}
static uintptr_t hp(uintptr_t sheet, uintptr_t delta, uintptr_t lane, uintptr_t d) {
    if (infinite_hp && (int32_t)delta < 0 && !(uint32_t)lane && active_sheet(sheet)) delta = 0;
    return hp_original(sheet, delta, lane, d);
}
static uintptr_t mp(uintptr_t actor, uintptr_t cost, uintptr_t c, uintptr_t d) {
    if (infinite_mp && actor && actor == *player && (int32_t)cost < 0) cost = 0;
    return mp_original(actor, cost, c, d);
}
static void recharge(uintptr_t sheet, float duration) {
    if (!infinite_mp || !active_sheet(sheet)) recharge_original(sheet, duration);
}
static uintptr_t scale(uintptr_t raw) {
    if (exp_mult == 1 || (int32_t)raw <= 0) return raw;
    uint64_t n = (uint64_t)(uint32_t)raw * exp_mult;
    return n > 9999999 ? 9999999 : n;
}
static uintptr_t award(uintptr_t amount, uintptr_t b, uintptr_t c, uintptr_t d) {
    return award_original(scale(amount), b, c, d);
}
static uintptr_t display(uintptr_t pos, uintptr_t amount, uintptr_t c, uintptr_t d) {
    return display_original(pos, scale(amount), c, d);
}

/* Inside the item roll, XMM1 holds the Lucky Lucky threshold and XMM0 the roll.
 * This leaf preserves GPRs, the guest stack and all other XMM registers. */
static _Atomic float drop_mult __attribute__((used));
static int drop_enabled __attribute__((used));
static float limit __attribute__((used)) = 100;
static void *volatile after_roll __attribute__((used));
__attribute__((naked,used)) static void drop_compare(void) {
    __asm__("mulss %xmm8,%xmm0\n"
            "cmpl $0,drop_enabled(%rip)\n"
            "je 1f\n"
            "mulss drop_mult(%rip),%xmm1\n"
            "minss limit(%rip),%xmm1\n"
            "1: jmp *after_roll(%rip)");
}
static void refill_drive(uintptr_t actor) {
    unsigned char *sheet = actor ? *(unsigned char **)(actor + 0x5c0) : NULL;
    if (!sheet) return;
    if (sheet[0x1b2]) {
        sheet[0x1b1] = sheet[0x1b2];
        sheet[0x1b0] = 100;
    }
    if (sheet[0x1af] == 2) memcpy(sheet + 0x1b4, sheet + 0x1b8, 4);
}
void cheats_frame(void) {
    if (infinite_drive) {
        refill_drive(*player);
        refill_drive(campaign_progress_actor());
    }
}
void cheats_refresh(void) {
    infinite_drive = rmc_int("cheats","cheat_infinite_drive");
    infinite_hp = rmc_int("cheats","cheat_infinite_hp");
    infinite_mp = rmc_int("cheats","cheat_infinite_mp");
    exp_mult = rmc_int("cheats","cheat_exp_mult");
    drop_mult = rmc_int("cheats","cheat_drop_mult");
    __atomic_store_n(&drop_enabled,drop_mult != 1,__ATOMIC_RELEASE);
}

void cheats_plan(void) {
    player = (uintptr_t *)rmc_site(0x2a105d0);
    cheats_refresh();
    rmc_hook(0x3c0860, hp, (void **)&hp_original);
    rmc_hook(0x3d3cf0, mp, (void **)&mp_original);
    rmc_hook(0x3c20a0, recharge, (void **)&recharge_original);
    rmc_hook(0x3ecf30, award, (void **)&award_original);
    rmc_hook(0x17ca70, display, (void **)&display_original);
    after_roll = rmc_site(0x3dc35d) + 5;
    rmc_hook(0x3dc35d, drop_compare, NULL);
}
