#include "look.h"
#include "campaign.h"
#include "tweaks_game.h"
#include <math.h>

#define VA(r) data_at(r)
#define ACTIVE VA(0x2a105d0)
#define PARKED VA(0x2a11268)
#define FC VA(0x2a10620)
#define MICKEY VA(0x7523b8)
#define call(r, a, b) call4(r, a, b, 0, 0)
const u8 *campaign_notice(int entering);
static int declined, save_interaction, death_pending;
static u64 xion, roxas, retiring, command_actor, command_place;
static u16 command_evt;
static s32 sheet_gauges[5], entry_hp, entry_mp;
static float actor_float(u64 address) {
    float f;
    memcpy(&f, (void *)address, 4);
    return f;
}
u64 campaign_progress_actor(void) { return xion ? roxas : 0; }
// Keep an unchanged resource exact across a round trip, including 1 HP on unlike sheets.
static s32 fraction(s32 value, s32 maximum, s32 destmax) {
    if (value <= 0 || maximum <= 0 || destmax <= 0)
        return 0;
    if (value >= maximum)
        return destmax;
    s32 scaled = (s32)((s64)value * destmax / maximum);
    return scaled ? scaled : 1;
}
static s32 carry_back(s32 value, s32 entry, s32 original, s32 maximum, s32 destmax) {
    return value == entry ? original : fraction(value, maximum, destmax);
}
/* Save-point recovery addresses the canonical party. Heal the playable sheet
 * so damage and recovery are ordered on the same gauge throughout the swap. */
u64 campaign_recovery_sheet(u64 sheet) {
    if (xion && sheet == rd64(roxas + 0x5c0))
        return rd64(xion + 0x5c0);
    return sheet;
}
static s32 return_gauge(u64 live, u64 parked, s32 entry) {
    return carry_back(rd32(live), entry, rd32(parked), rd32(live + 4), rd32(parked + 4));
}

/* f_180900 follows HUD slot zero to actor+0x5c0 on its next update. */
static void gauge_owner(u64 actor) {
    call(0x168850, 0, actor);
    if (call(0x1689a0, 0, 0) != actor)
        rmc_fail("xion-swap-hud-owner");
}

static void actor_method(u64 actor, u32 slot) {
    u64 desc = call(0x4ad270, rd32(actor), 0);
    ((Fn4)rd64(rd64(desc) + slot))(desc, actor, 0, 0);
}

void campaign_handback(void) {
    command_actor = 0;
    if (!xion)
        return;
    int lethal = death_pending;
    death_pending = 0;
    if (rd64(ACTIVE) != xion || rd64(PARKED) != roxas)
        rmc_fail("Xion handback lost player ownership");
    u64 xs = rd64(xion + 0x5c0), rs = rd64(roxas + 0x5c0);
    s32 hp = return_gauge(xs, rs, entry_hp);
    s32 mp = return_gauge(xs + 0x180, rs + 0x180, entry_mp);
    u64 pos = call(0x3b5b40, xion, 0);
    call(0x3b64d0, roxas, pos);
    ((void (*)(u64, float))rmc_site(0x3b6560))(roxas, actor_float(xion + 0x68c));
    // Native release writes these six bytes back into character 4's save record.
    wr32(xs, sheet_gauges[0]);
    wr32(xs + 4, sheet_gauges[1]);
    wr32(xs + 8, sheet_gauges[2]);
    wr32(xs + 0x180, sheet_gauges[3]);
    wr32(xs + 0x184, sheet_gauges[4]);
    // Weapon and effect children must leave the native parent list before their owner retires.
    while (rd64(xion + 0x8b8)) {
        u64 attachment = rd64(xion + 0x8b8);
        u64 child = attachment - 0x7b0;
        if (call(0x4ad270, rd32(child + 0x6a0), 0) != xion)
            rmc_fail("xion-swap-child: attachment does not belong to the retiring actor");
        call(0x3da250, child, 0);
        if (rd64(xion + 0x8b8) == attachment)
            rmc_fail("xion-swap-detach: native detach left the same child");
    }
    actor_method(roxas, 0x88);
    call(0x4151e0, MICKEY, xion); // descriptor slot +0x38, also pops the special command list
    retiring = xion;
    call(0x3a8140, 0, 0);
    call(0x3bd2e0, xion, roxas);
    gauge_owner(roxas);
    wr32(rs, hp);
    wr32(rs + 0x180, mp);
    call(0x3c2120, rs, 0);
    if (rd64(ACTIVE) != roxas || rd64(PARKED) || rd64(FC + 0xae0) != roxas)
        rmc_fail("Xion handback could not restore player ownership");

    wr32(xion + 0x120, rd32(xion + 0x120) | (1u << 19)); // native end-of-frame deletion queue
    u64 player = roxas;
    xion = roxas = 0;
    call(0x17b710, (u64)campaign_notice(0), 0);
    if (lethal) {
        u64 desc = call(0x4ad270, rd32(player), 0);
        ((Fn4)rd64(rd64(desc) + 0xb0))(desc, player, 0, 0);
    }
}

static void enter(u64 player, u64 original) {
    u64 row = call(0x3e2e40, 0, 0);
    if (!row || rd16(row + 0x24) != 792 || rd64(PARKED) || rd32(original + 0x260) != 1 ||
        (s32)rd32(original) <= 0)
        return;
    u64 ent = call(0x3dfeb0, 792, 0);
    if (!ent || rd16(ent + 0x4c) != 4) {
        declined = 1;
        return;
    }
    u32 ready = call(0x3e0160, 792, ent);
    if (!ready || (ready & ~12u)) {
        declined = 1;
        return;
    }
    u64 pos = call(0x3b5b40, player, 0);
    if (!pos || !rd32(VA(0x2a23950))) {
        declined = 1;
        return;
    }
    u64 actor = call(0x152430, 0xe60, 0);
    if (!actor) {
        declined = 1;
        return;
    }
    roxas = player;
    call(0x3a80e0, 0, 0);
    actor_method(roxas, 0x80); // trap_obj_hide: attachments still need their owner in the object list
    wr32(roxas + 0x9b8, rd32(roxas + 0x9b8) | 4); // action/damage gate cleared by 3a8178 on restore
    ((void (*)(u64, u64, int, u64, u64, float))rmc_site(0x3a7a40))(actor, ent, 11, rd64(roxas + 0xdb8), pos,
                                                                   actor_float(roxas + 0x68c));
    wr64(actor + 0xe08, 0);
    wr8(actor + 0xe10, 0);
    wr64(actor + 0xe18, 0);
    wr64(actor + 0xe50, 0);
    wr32(actor, call(0x4ad240, MICKEY, 0));
    wr32(actor + 0x6c8, rd32(actor + 0x6c8) | 0x2000000);
    u64 sheet = rd64(actor + 0x5c0);
    if (!sheet || rd64(ACTIVE) != actor || rd64(FC + 0xae0) != actor || rd64(FC + 0xc38) != actor) {
        actor_method(roxas, 0x88);
        call(0x4151e0, MICKEY, actor);
        retiring = actor;
        wr32(actor + 0x120, rd32(actor + 0x120) | (1u << 19));
        call(0x3a8140, 0, 0);
        roxas = 0;
        declined = 1;
        return;
    }
    sheet_gauges[0] = rd32(sheet);
    sheet_gauges[1] = rd32(sheet + 4);
    sheet_gauges[2] = rd32(sheet + 8);
    sheet_gauges[3] = rd32(sheet + 0x180);
    sheet_gauges[4] = rd32(sheet + 0x184);
    if (!sheet_gauges[1])
        wr32(sheet + 4, rd32(original + 4));
    if (!sheet_gauges[4])
        wr32(sheet + 0x184, rd32(original + 0x184));
    entry_hp = fraction((s32)rd32(original), rd32(original + 4), rd32(sheet + 4));
    entry_mp = fraction((s32)rd32(original + 0x180), rd32(original + 0x184), rd32(sheet + 0x184));
    wr32(sheet, entry_hp);
    wr32(sheet + 0x180, entry_mp);
    wr32(sheet + 8, 0); // permit genuine KO, the zero-result callback is MICKEY vfn22
    call(0x3bd2e0, roxas, actor);
    gauge_owner(actor);
    xion = actor;
    call(0x17b710, (u64)campaign_notice(1), 0);
}

static int supported(void) {
    static const struct {
        u8 world, room;
        u16 map, battle, event;
    } fields[] = {
#include "campaign_fields.inc"
    };
    u64 place = VA(0x717008);
    for (unsigned i = 0; i < sizeof fields / sizeof *fields; ++i)
        if (rd8(place) == fields[i].world && rd8(place + 1) == fields[i].room &&
            rd16(place + 4) == fields[i].map && rd16(place + 6) == fields[i].battle &&
            rd16(place + 8) == fields[i].event)
            return 1;
    return 0;
}
void campaign_command_done(u64 controller, u64 actor) {
    if (!campaign_mode() || controller != FC || rd64(ACTIVE) != actor || rd64(FC + 0xc38) != actor)
        return;
    command_actor = actor;
    command_place = rd64(VA(0x717008));
    command_evt = rd16(VA(0x717010));
}
#include "castle_rules.inc"

static int near_save(u64 player) {
    u64 obj = rd64(VA(0x2a171c8));
    for (int i = 0; obj && i < 512; ++i) {
        u32 id = rd32(obj + 8);
        u64 entry = (id >> 31) ? call(0x4ad270, id, 0) : 0;
        if (entry && !memcmp((void *)(entry + 8), "SAVE_POINT", 11)) {
            float x = actor_float(player + 0x670) - actor_float(obj + 0x670);
            float y = actor_float(player + 0x674) - actor_float(obj + 0x674);
            float z = actor_float(player + 0x678) - actor_float(obj + 0x678);
            if (x * x + y * y + z * z <= 80.f * 80.f)
                return 1;
        }
        obj = call(0x4ad270, rd32(obj + 0xa90), 0);
    }
    return 0;
}
/* The two ending PNACH jokers only clear the event lock when its low halfword equals 2. */
static void ending_pause(void) {
    if (!rmc_int("gameplay", "ending_pause") || rd16(VA(0xabb878)) != 2)
        return;
    u16 room = rd16(VA(0x717008)), event = rd16(VA(0x717010));
    if (room == 0x1412 && event == 74)
        wr8(VA(0x9ab540) + 0x24f, 0); /* progress flags 0x4878 through 0x487f */
    else if (room != 1 || event <= 50 || event >= 62)
        return;
    wr8(VA(0xabb878), 0);
}
void campaign_frame(void) {
    ending_pause();
    if (death_pending) {
        campaign_handback();
        return;
    }
    castle_frame();
    u64 actor = command_actor;
    command_actor = 0;
    if (!actor || !campaign_mode() || rd64(ACTIVE) != actor || rd64(FC + 0xae0) != actor ||
        rd64(FC + 0xc38) != actor || command_place != rd64(VA(0x717008)) ||
        command_evt != rd16(VA(0x717010)) || rd8(VA(0x9006b0)) || rd64(VA(0x2a11478)))
        return;
    int shop = rd16(VA(0x717008)) == 0x0702;
    if (shop) {
        float x = actor_float(actor + 0x670) + 1297, z = actor_float(actor + 0x678) + 2897;
        if (!(x * x + z * z >= 950.f * 950.f)) {
            campaign_handback();
            return;
        }
    }
    u64 pad = rd64(FC + 0xae8);
    if (xion && pad && ((rd64(pad) | rd64(pad + 8)) & (1u << 10)) && near_save(actor)) {
        save_interaction = 1;
        campaign_handback();
        return;
    }
    if (save_interaction) {
        if (near_save(actor))
            return;
        save_interaction = 0;
    }
    if (declined || xion || retiring || rd64(VA(0x9ba928)) || rd8(VA(0x9ba8d1)) || rd16(VA(0x9acdd4)) ||
        !supported())
        return;
    /* The native getter skips mission slot zero when scanning backwards. */
    if (rd16(VA(0x2a114a0)) || call(0x3ad500, 0, 0)) {
        u64 m = rd64(VA(0x2a0ff68)), header = m ? rd64(m + 8) : 0;
        if (!shop || !header || rd16(header + 2) != 0xa7)
            return;
    }
    u64 sheet = rd64(actor + 0x5c0), pool = VA(0x2a17290);
    if (sheet >= pool && sheet < pool + 80 * 0x278 && (sheet - pool) % 0x278 == 0)
        enter(actor, sheet);
}
static Fn4 preload_original, event_original, area_original, save_original, load_original, init_save_original;
static Fn5 depart_original;
static u64 preload(u64 a, u64 b, u64 c, u64 d) {
    u64 result = preload_original(a, b, c, d);
    declined = 0;
    save_interaction = 0;
    if (campaign_mode() && supported())
        call4(0x3e0460, 792, 3000, 7, 0);
    return result;
}
#define BOUNDARY(name)                                                                                       \
    static u64 name(u64 a, u64 b, u64 c, u64 d) {                                                            \
        campaign_handback();                                                                                 \
        return name##_original(a, b, c, d);                                                                  \
    }
BOUNDARY(event)
BOUNDARY(area)
BOUNDARY(save)
static u64 load(u64 a,u64 b,u64 c,u64 d) {
    campaign_handback();
    look_reset();
    return load_original(a,b,c,d);
}
static u64 init_save(u64 a,u64 b,u64 c,u64 d) {
    campaign_handback();
    look_reset();
    return init_save_original(a,b,c,d);
}
static u64 depart(u64 a, u64 b, u64 c, u64 d, u64 e) {
    campaign_handback();
    return depart_original(a, b, c, d, e);
}
static Fn4 start_original, rescue_a_original, rescue_b_original, eligible_original, player_original;
static u64 start(u64 a, u64 b, u64 c, u64 d) {
    if (__builtin_return_address(0) == (void *)VA(0x157304))
        campaign_handback();
    return start_original(a, b, c, d);
}
#define RESCUE(name)                                                                                         \
    static u64 name(u64 a, u64 b, u64 c, u64 d) {                                                            \
        if (xion && a == xion) {                                                                             \
            declined = 1;                                                                                    \
            campaign_handback();                                                                             \
            return 0;                                                                                        \
        }                                                                                                    \
        return name##_original(a, b, c, d);                                                                  \
    }
RESCUE(rescue_a)
RESCUE(rescue_b)
static u64 eligible(u64 a, u64 b, u64 c, u64 d) {
    if (!xion)
        return eligible_original(a, b, c, d);
    if ((u8)call(0x3a3c40, 0, 0))
        return 0;
    u32 leader = call(0x3e2e60, 0, 0);
    if (!(u8)call(0x3ba720, roxas, 0) || (u32)call(0x3b5b20, roxas, 0) != leader)
        return 0;
    return !(rd32(call(0x3d7470, roxas, 0) + 12) & (1u << 20));
}
static u64 player(u64 a, u64 b, u64 c, u64 d) {
    return xion && (u32)a == 1 ? roxas : player_original(a, b, c, d);
}
static u64 destroy(u64 desc, u64 actor, u64 c, u64 d) {
    if (!retiring || actor != retiring)
        return call4(0x414dd0, desc, actor, c, d);
    retiring = 0;
    call(0x2e2d60, actor, 0);
    return call(0x152570, actor, 0);
}
static u64 release_actor(u64 desc, u64 actor, u64 c, u64 d) {
    return retiring && actor == retiring ? 0 : call4(0x4151e0, desc, actor, c, d);
}
static u64 first_update(u64 desc, u64 actor, u64 c, u64 d) {
    if (!xion || actor != xion)
        return call4(0x414e30, desc, actor, c, d);
    call(0x3a8e70, actor, 0);
    return call(0x3db140, actor, 1);
}
static u64 update(u64 desc, u64 actor, u64 c, u64 d) {
    return xion && actor == xion ? call(0x3a92c0, actor, 0) : call4(0x414eb0, desc, actor, c, d);
}
static u64 late_update(u64 desc, u64 actor, u64 c, u64 d) {
    return xion && actor == xion ? call(0x3a89a0, actor, 0) : call4(0x4150e0, desc, actor, c, d);
}
static u64 killed(u64 desc, u64 actor, u64 c, u64 d) {
    if (!xion || actor != xion)
        return call4(0x415380, desc, actor, c, d);
    death_pending = declined = 1;
    return 0;
}
static void method(unsigned slot, unsigned original, Fn4 fn) {
    u64 before = rd64(VA(0x5d15a0) + slot);
#ifdef _WIN32
    u64 expected = VA(original);
#else
    u64 expected = 0x140000000ull + original; /* Offline PE check has preferred-base relocation values. */
#endif
    if (before != expected)
        rmc_fail("Xion native controller method mismatch");
    rmc_write_ptr((void *)(VA(0x5d15a0) + slot), &fn, sizeof fn);
}
void campaign_actor_plan(void) {
#define HOOK(r, f) rmc_hook(r, f, (void **)&f##_original)
    HOOK(0x3e2eb0, preload);
    HOOK(0x3ac350, event);
    HOOK(0x3a24c0, area);
    HOOK(0x152990, depart);
    HOOK(0x2ed110, save);
    HOOK(0x2ed0e0, load);
    HOOK(0x1523a0, init_save);
    HOOK(0x3ad540, start);
    HOOK(0x416030, rescue_a);
    HOOK(0x416100, rescue_b);
    HOOK(0x3ed110, eligible);
    HOOK(0x3d5c70, player);
#undef HOOK
    method(0, 0x414dd0, destroy);
    method(8, 0x414e30, first_update);
    method(16, 0x414eb0, update);
    method(32, 0x4150e0, late_update);
    method(56, 0x4151e0, release_actor);
    method(176, 0x415380, killed);
}
