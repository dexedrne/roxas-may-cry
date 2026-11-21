#include "campaign.h"
#include "rmc.h"
#include "config.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef uintptr_t word;
typedef word (*GameCall)(word, word, word, word);
#define CALL(id, a, b, c, d) ((GameCall)rmc_site(id))((word)(a), (word)(b), (word)(c), (word)(d))
#define FIELD(type, addr, offset) (*(type *)((uintptr_t)(addr) + (offset)))
typedef word (*Script)(uint64_t *);
struct Descriptor {
    Script invoke;
    uint64_t flags;
};
static struct Descriptor descriptors[114];
static float drive_scale;
void difficulty_plan(void);
void rmc_add_exp(uint64_t *args);

static word decode(uint32_t id) { return CALL(0x4ad270, id, 0, 0, 0); }
static uint32_t encode(word p) { return (uint32_t)CALL(0x4ad240, p, 0, 0, 0); }
static unsigned equipped(word addr, unsigned count, uint16_t ability) {
    unsigned total = 0;
    for (unsigned i = 0; i < count; ++i) {
        uint16_t v = FIELD(uint16_t, addr, i * 2);
        if ((v & 0x8000) && (uint16_t)((int16_t)(v << 1) >> 1) == ability)
            ++total;
    }
    return total;
}
static int item_ability(int16_t id, uint16_t ability, int allow_missing) {
    word p = CALL(0x3e39a0, (int32_t)id, 0, 0, 0);
    if (!p && allow_missing)
        return 0;
    if (!p)
        rmc_fail("Missing party weapon");
    p = CALL(0x3e3ef0, p, 0, 0, 0);
    return p && FIELD(uint16_t, p, 2) == ability;
}
static void ability_count(uint64_t *args) {
    uint16_t ability = (uint16_t)args[1];
    word actor = decode(args[0]), sheet = FIELD(word, actor, 0x5c0);
    uint32_t party_id = FIELD(uint32_t, sheet, 0x250);
    if (!party_id)
        return;
    word party = decode(party_id), form = decode(FIELD(uint32_t, sheet, 0x254));
    int16_t weapon = FIELD(int16_t, party, 0);
    unsigned n = item_ability(weapon, ability, 0);
    for (int k = 0; k < 2; ++k)
        for (int i = 0; i < FIELD(int8_t, party, 0x10 + k); ++i) {
            int16_t id = FIELD(int16_t, party, 0x14 + 16 * k + 2 * i);
            if (id)
                n += item_ability(id, ability, 1);
        }
    n += equipped(party + 0x54, 80, ability);
    if (form) {
        if (FIELD(uint16_t, form, 0))
            n += item_ability(weapon, ability, 1);
        n += equipped(form + 8, 24, ability);
    }
    memcpy(args, &n, 4);
}
static void script_write_byte(word p, unsigned char value) {
    word table = *(word *)rmc_site(0x2a161e8);
    if (table) {
        int32_t count = FIELD(int32_t, table, 4);
        size_t offset = p - (table + 8);
        if (count > 0 && offset < (size_t)count * 48 && offset % 48 == 47) {
            *(unsigned char *)p = value;
            return;
        }
    }
    /* Both tt28 drive tutorials call trap_sysobj_player then trap_obj_sheet. */
    word player = CALL(0x3b3e80, 0, 0, 0, 0);
    word actor = player ? decode(FIELD(uint32_t, player, 4)) : 0;
    word sheet = actor ? FIELD(word, actor, 0x5c0) : 0;
    if (!sheet || (p != sheet + 0x1b1 && p != sheet + 0x1b2))
        rmc_fail("Script byte write outside a command reserve or player drive field");
    *(unsigned char *)p = value;
}
static word dispatch(unsigned idx, const char *name, uint64_t *args) {
    uint32_t a = (uint32_t)args[0], b = 0, result;
    if ((descriptors[idx].flags & 0xffff) > 1)
        b = (uint32_t)args[1];
    word p;
    switch (idx) {
    case 0:
        return CALL(0x3b4da0, decode(a), b, 0, 0);
    case 1:
        rmc_add_exp(args);
        return 0;
    case 3:
        result = encode(((uint64_t)(int64_t)(int32_t)a << 32) | (uint64_t)(int64_t)(int32_t)b);
        break;
    case 8: {
        struct {
            uint8_t area[4];
            uint16_t program[3];
        } rec = {{a, b, args[2], 0}, {args[3], args[4], args[5]}};
        typedef word (*Jump)(void *, uint32_t, uint32_t, int, uint32_t);
        return ((Jump)rmc_site(0x152990))(&rec, args[6], args[7], 0, args[8]);
    }
    case 13:
        return CALL(0x3c0cd0, decode(a), b, 0, 0);
    case 15:
        result = encode(CALL(0x3eafc0, a, b, 0, 0));
        break;
    case 33:
        return CALL(0x3c2100, decode(a), 0, 0, 0);
    case 34:
    case 35:
        p = CALL(0x39b760, 0, 0, 0, 0);
        result = p && (FIELD(uint64_t, p, idx == 34 ? 8 : 0) & (uint64_t)(int64_t)(int32_t)(1u << (a & 31)));
        break;
    case 46:
        ability_count(args);
        return 0;
    case 47:
        result = encode((word)rmc_site(0x5c9030));
        break;
    case 48:
    case 50:
        p = CALL(0x3b3e80, 0, 0, 0, 0);
        p = FIELD(word, p, 0xdd0);
        result = FIELD(uint8_t, p, 0xb42);
        break;
    case 55:
        if (!*(word *)rmc_site(0x2ae5768))
            rmc_fail("Camera mode requested before system load");
        *rmc_site(0x718ca8) = (unsigned char)a;
        return 0;
    case 69:
        script_write_byte(decode(a), (unsigned char)b);
        return 0;
    case 78:
        result = (uint32_t)CALL(0x3c4720, a, 0, 0, 0);
        break;
    case 80:
        return CALL(0x3e4530, a, 0, 0, 0);
    case 87:
        memcpy(&drive_scale, &a, 4);
        return 0;
    case 93:
        return CALL(0x3cb9f0, 0, CALL(0x3b3e80, 0, 0, 0, 0), 1, 0);
    default: {
        char reason[128];
        snprintf(reason, sizeof reason, "Unsupported script table 3 entry %u (%s)", idx, name);
        rmc_fail(reason);
        return 0;
    }
    }
    memcpy(args, &result, 4);
    return result;
}
#define ENTRY(index, name, flags)                                                                            \
    static word script_##index(uint64_t *slots) { return dispatch(index, #name, slots); }
#include "script_entries.inc"
#undef ENTRY
static struct Descriptor descriptors[] = {
#define ENTRY(index, name, flags) [index] = {script_##index, flags},
#include "script_entries.inc"
#undef ENTRY
};
static word reset_table(word a, word b, word c, word d) {
    *(struct Descriptor **)rmc_site(0x7534a8) = descriptors;
    return CALL(0x3e1620, a, b, c, d);
}
static void add_drive(word actor, float amount) {
    word canonical = campaign_progress_actor();
    if (canonical && actor == *(word *)rmc_site(0x2a105d0) &&
        __builtin_return_address(0) == (void *)rmc_site(0x3dcae8)) actor = canonical;
    typedef float (*GetRate)(word);
    typedef void (*SetAmount)(word, float);
    word sheet = FIELD(word, actor, 0x5c0);
    float rate = ((GetRate)rmc_site(0x3a3990))(actor);
    unsigned mode = FIELD(uint8_t, sheet, 0x1af);
    if (mode == 1) {
        if (FIELD(float, sheet, 0x1c0) > 0) {
            word params = CALL(0x3e9300, 2, "sstm", 0, 0);
            amount *= FIELD(float, params, 0x80);
            amount *= FIELD(float, sheet, 0x20c);
        }
        amount *= drive_scale;
        amount *= rate;
        int32_t n = (!isfinite(amount) || amount >= 2147483648.f || amount < -2147483648.f) ? INT32_MIN
                                                                                            : (int32_t)amount;
        CALL(0x3c0ad0, sheet, (uint32_t)n, 0, 0);
    } else if (mode == 3 || (mode == 2 && FIELD(int32_t, actor, 0xde0) != 12)) {
        word params = CALL(0x3e9300, 2, "sstm", 0, 0);
        ((SetAmount)rmc_site(0x3c0b50))(sheet, amount * FIELD(float, params, 0x78));
    }
}
void gameplay_plan(void) {
    if (*(word *)rmc_site(0x7534a8))
        rmc_fail("Script table 3 already occupied");
    struct Descriptor *table = descriptors;
    rmc_write(0x7534a8, &table, sizeof table);
    drive_scale = (float)rmc_value("combat", "drive_add_mult", 0);
    rmc_hook(0x1f9a80, reset_table, NULL);
    rmc_hook(0x3a9cf0, add_drive, NULL);
    difficulty_plan();
}
