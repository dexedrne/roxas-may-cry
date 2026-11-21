#include "campaign.h"
#include "difficulty.h"
#include "tweaks_game.h"
#include <math.h>

static struct difficulty_config {
    s32 enemy_damage, party_damage;
    float ap, hp, mp;
} config[4];

Fn4 rmc_stock_damage;
static Fn4 stock_usable;
static int boosted_party, protected_party;

void rmc_debug_damage(int enemy, int active) {
    if (enemy)
        protected_party = active;
    else
        boosted_party = active;
}

static float damage_rf(u64 p) {
    u32 v = rd32(p);
    float f;
    memcpy(&f, &v, 4);
    return f;
}
static s32 damage_cvtt(float f) {
    return !isfinite(f) || f >= 2147483648.0f || f < -2147483648.0f ? INT32_MIN : (s32)f;
}
static u64 damage_decode(u32 id) { return call4(0x4ad270, id, 0, 0, 0); }
static int damage_actor(u64 p) { return (u8)call4(0x3ba720, p, 0, 0, 0) != 0; }
static s32 damage_mul(s32 a, s32 b) { return (s32)((u32)a * (u32)b); }
static s32 damage_add(s32 a, s32 b) { return (s32)((u32)a + (u32)b); }
static u64 damage_sstm(void) { return call4(0x3e9300, 2, "sstm", 0, 0); }

/* Capture the value in the caller's RDX home slot before any C prologue.
   The four-argument caller did not allocate room for a fifth argument. */
#ifdef _WIN32
extern u32 rmc_damage_entry(u64, u64, u64, u64);
__asm__(".text\n.globl rmc_damage_entry\n.seh_proc rmc_damage_entry\n"
        "rmc_damage_entry:\n subq $40, %rsp\n.seh_stackalloc 40\n.seh_endprologue\n"
        "movl %r9d, 72(%rsp)\n movl 56(%rsp), %eax\n movl %eax, 32(%rsp)\n"
        "call rmc_damage_body\n addq $40, %rsp\n ret\n.seh_endproc");
#endif

u32 rmc_damage_body(u64 victim, u64 packet, u64 attack, s32 lane, s32 home) {
    u32 difficulty = rd32(data_at(0x9abd48));
    u64 attacker = damage_decode(rd32(packet + 0x10));
    u64 sheet = rd64(attacker + 0x5c0);
    s32 base, upper, lower, result;
    if (sheet) {
        if (!attack) {
            base = upper = lower = home;
        } else {
            u8 kind = rd8(attack + 5);
            if (kind == 0) {
                base = (s16)rd16(sheet + 0x188);
                if (damage_rf(sheet + 0x1c0) > 0)
                    base = damage_add(base, (s16)rd16(sheet + 0x226));
            } else if (kind == 1) {
                base = (s16)rd16(sheet + 0x18a);
                u8 type = rd8(attack + 4);
                if (type == 5) {
                    s32 n = damage_mul((s16)rd16(attack + 6), (s32)rd32(victim + (s64)lane * 12 + 4));
                    result = damage_add(damage_mul(n, base), 9999) / 10000;
                    goto done;
                }
                if (type == 6) {
                    result = damage_add(damage_mul((s16)rd16(attack + 6), base), 99) / 100;
                    goto done;
                }
            } else if (kind == 2)
                base = (s16)rd16(sheet + 0x18c);
            else
                base = home;
            upper = rd32(sheet + 0x198);
            lower = rd32(sheet + 0x19c);
        }
    } else {
        u64 params = call4(0x3ee8c0, 0, 0, 0, 0);
        base = (s16)rd16(params + 2);
        upper = (s16)rd16(params + 6);
        lower = (s16)rd16(params + 8);
    }
    base = damage_add(base, -(s32)(s16)rd16(victim + 0x18c));
    u64 stat = victim + (s64)lane * 12;
    if ((s32)rd32(stat) <= (s32)rd32(stat + 4) / 4)
        base = damage_add(base, -(s32)(s8)rd8(victim + 0x204));
    s32 cap = rd32(victim + 0x1a0);
    if (upper >= cap)
        upper = cap;
    if (base >= upper)
        base = upper;
    cap = rd32(victim + 0x1a4);
    if (lower <= cap)
        lower = cap;
    if (base <= lower)
        base = lower;
    s32 n = damage_mul(rd8(victim + 0x1a8 + (s8)rd8(attack + 9)), rd8(victim + 0x1ae));
    n = damage_mul(n, (s16)rd16(attack + 6));
    n = damage_mul(n, base);
    result = damage_add(n, 999999) / 1000000;
    u64 save = call4(0x152380, 0, 0, 0, 0);
    if (save) {
        u64 victim_actor = damage_decode(rd32(victim + 0x268));
        if (damage_actor(victim_actor)) {
            /* DLL 330d1..33151: the values 0, 1, 2, 3 of [save+2498] load the ini pairs; every other value
             * takes 100 / 100 (330ea `mov r14d,0x64; mov esi,r14d`) and the scaling below still runs. */
            s32 enemy = 100, party = 100;
            if (difficulty <= 3) {
                enemy = protected_party ? 0 : config[difficulty].enemy_damage;
                party = boosted_party ? 9999 : config[difficulty].party_damage;
            }
            if (rd8(victim_actor + 0x6c8) & 4)
                result = damage_add(damage_mul(result, enemy), 99) / 100;
            else if (damage_actor(attacker) && (rd8(attacker + 0x6c8) & 4))
                result = damage_add(damage_mul(result, party), 99) / 100;
        }
    }
    result = call4(0x3c10e0, victim, (u32)result, 0, 0);
    if (sheet) {
        s32 element = (s8)rd8(attack + 9), bonus;
        if (element >= 1 && element <= 3) {
            bonus = (s16)rd16(sheet + 0x218 + 2 * (element - 1));
            /* As shipped: +99 precedes the multiplication, unlike vanilla. */
            result = damage_mul(damage_add(bonus, 99), result) / 100;
        }
        result = call4(0x3c0eb0, victim, (u32)result, sheet, attack);
    }
    if ((s32)rd32(stat) <= damage_cvtt((float)(s32)rd32(stat + 4) * damage_rf(victim + 0x248)))
        result = damage_add(damage_mul(result, (s32)rd32(damage_sstm() + 0x180)), 99) / 100;
done:
    return (u32)result;
}
#define reward_call(id, a, b, c) call4(id, a, b, c, 0)
static void reward_message(u64 list, int *n, u32 id) {
    wr64(list + 8 * (*n)++, reward_call(0x3ad9a0, id, 0, 0));
}
static void level_reward(u64 row, u32 character, u32 level, s32 path) {
    u32 d = rd32(data_at(0x9abd48));
    if (d > 3) {
        rmc_fail("level reward difficulty outside 0..3");
        return;
    }
    u64 messages[11] = {0}, list = (u64)messages;
    u64 sheet = reward_call(0x3c04c0, character, 0xffffffff, 0);
    int n = 0;
    u8 flags = rd8(row + 14), packed = rd8(row + 15);
    if (flags & 1) {
        reward_message(list, &n, 0x8ae2);
        s32 amount = (s32)(((float)(packed >> 4) * config[d].hp) * 0.5f);
        wr32(sheet + 4, rd32(sheet + 4) + (u32)amount);
    }
    if (flags & 2) {
        reward_message(list, &n, 0x8ae3);
        /* The DLL deliberately uses HP's multiplier for both level-up grants. */
        s32 amount = (s32)(((float)(packed & 15) * config[d].hp) * 0.5f);
        wr32(sheet + 0x184, rd32(sheet + 0x184) + (u32)amount);
    }
    if (flags & 4) {
        reward_message(list, &n, 0x8ae4);
        wr8(sheet + 0x1b2, rd8(sheet + 0x1b2) + 1);
    }
    const u8 slot_flags[] = {8, 16, 32}, slot_offsets[] = {0x12, 0x11, 0x10};
    for (int k = 0; k < 3; k++)
        if (flags & slot_flags[k]) {
            reward_message(list, &n, 0x8ae5 + k);
            u64 party = reward_call(0x3a14e0, data_at(0x9abda0), character, 0);
            wr8(party + slot_offsets[k], rd8(party + slot_offsets[k]) + 1);
        }
    for (int k = 0; k < 3; k++)
        if ((s8)rd8(row + 4 + k) > (s8)rd8(row - 12 + k))
            reward_message(list, &n, 0x8ade + k);
    s32 before = (s32)((float)(s8)rd8(row - 9) * config[d].ap);
    s32 after = (s32)((float)(s8)rd8(row + 7) * config[d].ap);
    if (after > before) {
        /* DLL 322ce writes AP's message without incrementing the list index. */
        wr64(list + 8 * n, reward_call(0x3ad9a0, 0x8ae1, 0, 0));
    }
    s32 ability = (s16)rd16(row + 8 + 2 * (s64)path);
    if (ability) {
        reward_call(0x3c3f00, (u32)ability, character, 0);
        if (rd32(list + 16) < 2)
            reward_call(0x3ed400, list, (u32)ability, 0);
    }
    u64 party = reward_call(0x3e2e20, 0xffffffff, 0, 0);
    if ((u8)reward_call(0x3e37a0, party, character, 0))
        reward_call(0x3eda60, list, character, level);
    reward_call(0x3c07e0, sheet, 0, 0);
}
static void bonus_reward(u64 row, u64 sheet) {
    u32 d = rd32(data_at(0x9abd48));
    if (d > 3) {
        rmc_fail("bonus reward difficulty outside 0..3");
        return;
    }
    s32 param = (s8)rd8(row + 13);
    if (param)
        reward_call(0x3edca0, (u32)param, 0, 0);
    if (rd8(row + 12) && !(rd8(row + 12) & (1u << d))) {
        return;
    }
    u64 messages[11] = {0}, list = (u64)messages;
    reward_call(0x3ecf20, list, 0, 0);
    s32 hp_grant = (s8)rd8(row + 2), mp_grant = (s8)rd8(row + 3);
    u8 character = rd8(row + 1);
    if ((u8)(character - 1) <= 2 || character == 14) {
        hp_grant = (s32)((float)hp_grant * config[d].hp);
        mp_grant = (s32)((float)mp_grant * config[d].mp);
    }
    wr32(sheet + 4, rd32(sheet + 4) + (u32)hp_grant);
    wr32(sheet + 0x184, rd32(sheet + 0x184) + (u32)mp_grant);
    call4(0x3c0c30, sheet, (u32)hp_grant, 0, 0);
    reward_call(0x3c0cd0, sheet, (u32)mp_grant, 0);
    s32 drive = (s8)rd8(row + 4) + rd8(sheet + 0x1b2);
    wr8(sheet + 0x1b2, drive < 9 ? drive : 9);
    reward_call(0x3c0ad0, sheet, 0, 0);
    u32 id = rd32(sheet + 0x250);
    if (id) {
        u64 party = reward_call(0x4ad270, id, 0, 0);
        reward_call(0x3a16c0, party, (u32)((s8)rd8(party + 0x12) + (s8)rd8(row + 5)), 0);
        wr8(party + 0x11, rd8(party + 0x11) + rd8(row + 6));
        wr8(party + 0x10, rd8(party + 0x10) + rd8(row + 7));
    }
    /* The stock popup has two pointer slots followed by its write index. */
    reward_call(0x3ed3d0, list, (u32)hp_grant, 0x8ae2);
    reward_call(0x3ed3d0, list, (u32)mp_grant, 0x8ae3);
    for (int k = 4; k < 8 && rd32(list + 16) < 2; k++)
        reward_call(0x3ed3d0, list, (u32)(s32)(s8)rd8(row + k), 0x8ae0 + k);
    for (int k = 8; k <= 10; k += 2) {
        s32 ability = (s16)rd16(row + k);
        if (ability) {
            reward_call(0x3c3f00, (u32)ability, rd32(sheet + 0x260), 0);
            if (rd32(list + 16) < 2)
                reward_call(0x3ed400, list, (u32)ability, 0);
        }
    }
    u64 actor = reward_call(0x4ad270, rd32(sheet + 0x268), 0, 0);
    if (actor)
        reward_call(0x3ed350, actor, 0, 0);
    reward_call(0x3edb20, list, (u32)(s32)(s8)character, (u32)(s32)(s8)rd8(data_at(0x9acfb0)));
}
static s32 item_div(s32 n, int reversed) {
    s32 q = (s32)(((s64)(reversed ? (s32)0x97247453 : (s32)0x68db8bad) * (s64)n) >> 32) >> 12;
    return (s32)((u32)q + ((u32)q >> 31));
}
static u64 item_enter(u32 target) {
    u64 sheet = call4(0x3c04c0, target, 0xffffffffu, 0, 0);
    if (!sheet) {
        rmc_fail("item effect has no character sheet");
        return 0;
    }
    call4(0x4ad270, rd32(sheet + 0x268), 0, 0, 0);
    call4(0x152380, 0, 0, 0, 0);
    return sheet;
}
static void item_hp(u64 sheet, s32 amount, int flag) { call4(0x3c0c30, sheet, (u32)amount, 0, flag); }
static void item_mp(u64 sheet, s32 amount, int flag) { call4(0x3c0cd0, sheet, (u32)amount, flag, 0); }
static u64 item_stock_apply(u64 rec, u32 target, u32 user, u32 mult) {
    u64 sheet = item_enter(target);
    unsigned id = rd16(rec), pct = rd8(rec + 4);
    int flag = user != 0;
    /* Both numerators are formed before the command lookup, from the maxima as they are at that point. */
    s32 mp_n = damage_add(damage_mul(damage_mul((s32)pct, (s32)rd32(sheet + 0x184)), (s32)mult), 9999);
    s32 hp_n = damage_add(damage_mul(damage_mul((s32)pct, (s32)mult), (s32)rd32(sheet + 4)), 9999);
    u64 cmd = call4(0x3b68b0, (u32)(s32)(s16)rd16(rec + 0x10), 0, 0, 0);
    if (!cmd) {
        rmc_fail("item command record missing");
        return 0;
    }
    int reversed = rd8(cmd + 0x2f) == 2;
    s32 hp_amount = item_div(hp_n, reversed), mp_amount = item_div(mp_n, reversed);
    /* 314e0..31557: (id-1)&0xfffa==0 selects 1,2,5,6 and 6 is then excluded. */
    if (!((id - 1) & 0xfffa) && id != 6)
        item_hp(sheet, hp_amount, flag);
    if (id == 3 || id == 6)
        item_mp(sheet, mp_amount, flag);
    if (id == 4 || id == 7) {
        item_hp(sheet, hp_amount, flag);
        item_mp(sheet, mp_amount, 0);
    }
    if (id == 0x83) {
        item_hp(sheet, (s32)rd32(sheet + 4), 1);
        /* As shipped: no +9999 and no division, and max MP is read again after the HP call. */
        item_mp(sheet, damage_mul(damage_mul((s32)pct, (s32)rd32(sheet + 0x184)), (s32)mult), 0);
    }
    if (id == 0x112 || id == 0x113)
        call4(0x3c0ad0, sheet, (u32)damage_mul((s32)pct, 100), 0, 0);
    if (id == 0x114)
        call4(0x3c09d0, sheet, pct, 0, 0);
    if (id == 0x115)
        call4(0x3c0e30, sheet, pct, 0, 0);
    if (id == 0x116)
        call4(0x3c0a50, sheet, pct, 0, 0);
    if (id == 0x117)
        call4(0x3c0950, sheet, pct, 0, 0);
    return call4(0x3c07e0, sheet, 0, 0, 0);
}
static int item_cap(unsigned kind, int difficulty) { return difficulty != 3 ? 240 : kind == 11 ? 120 : 220; }
static int item_in_scope(unsigned kind, int apply) {
    return !(apply ? ((kind >= 1 && kind <= 24) || kind == 30 || kind == 31)
                   : (kind >= 1 && kind <= 24 && kind != 22)) ||
           kind == 11 || kind == 12;
}
static u64 item_apply(u64 effect, u32 target, u32 user, u32 mult) {
    unsigned kind = rd8(effect + 5);
    if (!kind)
        return item_stock_apply(effect, target, user, mult);
    if (!item_in_scope(kind, 1)) {
        rmc_fail("unsupported item application kind");
        return 0;
    }
    if (kind != 11 && kind != 12)
        return call4(0x3c07e0, item_enter(target), 0, 0, 0);
    u64 sheet = call4(0x3c04c0, target, UINT32_MAX, 0, 0);
    if (sheet) {
        call4(0x4ad270, rd32(sheet + 0x268), 0, 0, 0);
        u64 save = call4(0x152380, 0, 0, 0, 0);
        u64 field = sheet + (kind == 11 ? 4 : 0x184);
        if ((s32)rd32(field) < item_cap(kind, (s32)rd32(save + 0x2498)))
            wr32(field, rd32(field) + rd8(effect + 4));
    }
    return call4(0x3c07e0, sheet, 0, 0, 0);
}
static u64 item_usable(u64 effect, u64 target, u64 a, u64 b) {
    unsigned kind = rd8(effect + 5);
    if (!item_in_scope(kind, 0)) {
        rmc_fail("unsupported item usability kind");
        return 0;
    }
    u64 sheet = call4(0x3c04c0, (u32)target, UINT32_MAX, 0, 0);
    if (sheet && (kind == 11 || kind == 12)) {
        u64 save = call4(0x152380, 0, 0, 0, 0);
        if ((s32)rd32(sheet + (kind == 11 ? 4 : 0x184)) < item_cap(kind, (s32)rd32(save + 0x2498)))
            return 1;
    }
    return stock_usable(effect, target, a, b);
}
void rmc_add_exp(u64 *arguments) {
    u64 args = (u64)arguments;
    u64 actor = damage_decode(rd32(args));
    s32 input = rd32(args + 8);
    if (!(u8)call4(0x3ed110, 0, 0, 0, 0))
        return;
    _Alignas(16) float position[4] = {0};
    u64 vec = (u64)position;
    call4(0x3b58c0, actor, vec, 0, 0);
    u64 params = call4(0x3ee8c0, 0, 0, 0, 0);
    s32 award = damage_mul(damage_mul((s16)rd16(params + 10), input), 100) / 10000;
    u64 player = call4(0x3b3e80, 0, 0, 0, 0), sheet = rd64(player + 0x5c0);
    float ratio = (float)(s32)rd32(sheet) / (float)(s32)rd32(sheet + 4);
    u64 canonical = campaign_progress_actor();
    if (canonical) sheet = rd64(canonical + 0x5c0);
    if (damage_rf(damage_sstm() + 0x14c) >= ratio)
        award = damage_mul(award, (s8)rd8(sheet + 0x203));
    u64 save = call4(0x152380, 0, 0, 0, 0);
    u32 d = rd32(save + 0x2498);
    if (d == 3) {
        award = damage_mul(award, 75) / 100;
        if (!award)
            award = 1;
    }
    s32 levels = call4(0x3ecf30, (u32)award, 0, 0, 0);
    if (levels > 0)
        call4(0x17ca70, vec, (u32)award, (u32)levels, 0);
}
void difficulty_plan(void) {
    static const char *levels[] = {"beginner", "standard", "proud", "crit"};
    for (unsigned i = 0; i < 4; i++) {
        char key[40];
#define FIELD(member, suffix)                                                                                \
    do {                                                                                                     \
        snprintf(key, sizeof key, "%s_%s", levels[i], suffix);                                               \
        config[i].member = rmc_value("combat", key, 0);                                                      \
    } while (0)
        FIELD(enemy_damage, "enemy_dmg");
        FIELD(party_damage, "party_dmg");
        FIELD(ap, "ap_mult");
        FIELD(hp, "hp_mult");
        FIELD(mp, "mp_mult");
#undef FIELD
    }
#ifndef _WIN32
    /* The host recorder calls the C body with an explicit home-slot value. */
#define rmc_damage_entry rmc_damage_body
#endif
    rmc_hook(0x3c12f0, rmc_damage_entry, (void **)&rmc_stock_damage);
    rmc_hook(0x3ed7d0, level_reward, NULL);
    rmc_hook(0x3edf90, bonus_reward, NULL);
    rmc_hook(0x3c4b80, item_apply, NULL);
    rmc_hook(0x3c3da0, item_usable, (void **)&stock_usable);
}
