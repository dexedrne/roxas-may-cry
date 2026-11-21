#include "tweaks_game.h"
#include <stdatomic.h>

typedef struct {
    u64 R, C, A;
    float lower_bound;
} Mp;
#define REQUIRE(condition, reason)                                                                           \
    do {                                                                                                     \
        if (!(condition)) {                                                                                  \
            fputs(reason, stderr);                                                                           \
            abort();                                                                                         \
        }                                                                                                    \
    } while (0)
static float read_float(u64 at) {
    float f;
    memcpy(&f, (void *)at, 4);
    return f;
}
static void write_float(u64 at, float value) { memcpy((void *)at, &value, 4); }
static u64 mask_bit(u32 bit) { return (u64)(s64)(s32)(1u << (bit & 31)); }
static void motion_call(unsigned id, u64 a, u32 b, float c, float d, u64 e) {
    ((void (*)(u64, u32, float, float, u64))rmc_site(id))(a, b, c, d, e);
}

struct Saved {
    u64 actor;
    int value;
    struct Saved *next;
};
static struct Saved *maps[2];
static atomic_flag map_lock = ATOMIC_FLAG_INIT;
static void mp_override(Mp *m, int which, u32 value) {
    u64 actor = m->A;
    REQUIRE(actor, "motion override requires an actor");
    int active = (float)((s32)rd8(m->R + 2) - 1) > read_float(m->C + 0x44);
    while (atomic_flag_test_and_set_explicit(&map_lock, memory_order_acquire)) {
    }
    struct Saved **where = &maps[which];
    while (*where && (*where)->actor != actor)
        where = &(*where)->next;
    if (active || *where) {
        u64 sheet = rd64(actor + 0x5c0);
        REQUIRE(sheet, "motion override requires a status sheet");
        if (active) {
            if (!*where) {
                *where = malloc(sizeof **where);
                REQUIRE(*where, "motion override allocation failed");
                **where = (struct Saved){actor, which ? rd8(sheet + 0x1ae) : (s32)rd32(sheet + 8), NULL};
            }
            if (which)
                wr8(sheet + 0x1ae, value);
            else
                wr32(sheet + 8, (u32)(s32)(s16)value);
        } else {
            struct Saved *entry = *where;
            if (which)
                wr8(sheet + 0x1ae, entry->value);
            else
                wr32(sheet + 8, entry->value);
            *where = entry->next;
            free(entry);
        }
    }
    atomic_flag_clear_explicit(&map_lock, memory_order_release);
}
static void range_extension(Mp *m, s32 type) {
    u64 R = m->R, C = m->C, A = m->A;
    u32 P1 = rd16(R + 6), P2 = rd16(R + 8), P3 = rd16(R + 10);
    s32 P4 = (s16)rd16(R + 12);
    switch (type) {
    case 54:
        REQUIRE(A, "actor pointer is null");
        wr64(A + 0x120, rd64(A + 0x120) & ~0x200000000ull);
        break;
    case 55:
        REQUIRE(A, "actor pointer is null");
        wr64(A + 0x120, rd64(A + 0x120) | 0x200000000ull);
        break;
    case 56:
    case 57:
    case 58:
    case 59:
        wr8(C + 0x154, rd8(C + 0x154) | (u8)(0x10 << (type - 56)));
        break;
    case 60: {
        u64 S = call4(0x39b760, 0, 0, 0, 0);
        if (!S || (u16)P1 || ((u16)P2 && !(rd64(S + 8) & mask_bit(P2))))
            break;
        REQUIRE(A, "actor pointer is null");
        if (!((rd32(A + 0x6c8) >> 7) & 1))
            break;
        u64 V = rd64(A + 0xdc8);
        call5(0x3bde30, V + 4, 0, 0, 0, 0);
        u64 T = call5(0x3d74e0, A, (u32)(s32)(s16)P3, 0, 0, 0);
        if (T) {
            call5(0x3d78d0, A, T, V + 4, 0, 0);
        }
        break;
    }
    case 61: {
        u64 S = call4(0x39b760, 0, 0, 0, 0);
        if (!S || (u16)P1 || ((s16)P2 && !(rd64(S + 8) & mask_bit(P2))))
            break;
        motion_call(0x3c6dc0, C, (u32)(s32)(s16)P3, 0.0f, 0.0f, 0);
        motion_call(0x3c8310, C, (u32)P4, 0.0f, 0.0f, 0);
        break;
    }
    case 62: {
        u32 P5 = rd16(R + 14);
        REQUIRE(A, "actor pointer is null");
        u64 sheet = rd64(A + 0x5c0);
        u64 S = call4(0x39b760, 0, 0, 0, 0);
        REQUIRE(sheet, "actor sheet A[+0x5c0] is null");

        s32 M = (s32)1 >> (P1 & 31);
        if (!(rd32(sheet + 0x1d0) & (u32)M) || !S || (u16)P2 || ((u16)P3 && !(rd64(S + 8) & mask_bit(P3))))
            break;
        motion_call(0x3c6dc0, C, (u32)P4, 0.0f, 0.0f, 0);
        motion_call(0x3c8310, C, (u32)(s32)(s16)P5, 0.0f, 0.0f, 0);
        break;
    }
    case 63:
        break;
    case 64:
        mp_override(m, 0, P1);
        break;
    case 65:
        mp_override(m, 1, P1);
        break;
    case 66:
    case 67:
    case 68: {
        REQUIRE(A, "actor pointer is null");
        u64 sheet = rd64(A + 0x5c0);
        REQUIRE(sheet, "actor sheet A[+0x5c0] is null (callee dereferences it)");
        u32 v = (u32)(s32)(s16)P1;
        if (type == 66)
            call5(0x3c0ad0, sheet, v, 0, 0, 0);
        else if (type == 67)
            call5(0x3c0c30, sheet, v, 0, 0, 0);
        else
            call5(0x3c0cd0, sheet, v, 0, 0, 0);
        break;
    }
    case 69:
        break;
    case 70: {
        REQUIRE(A, "actor pointer is null");
        u32 id = (u32)call5(0x4ad240, A, 0, 0, 0, 0);
        u64 slots[3] = {0};
        u64 buf = (u64)slots;
        wr32(buf, id);
        wr32(buf + 8, (u32)(s32)(s16)P1);
        wr32(buf + 16, (u32)(s32)(s16)P2);
        call5(0x3e1840, rd64(A + 0x5b0), 0x11, buf, 3, 0);
        break;
    }
    case 71:
    case 72:
    case 73:
    case 74:
    case 75:
    case 76:
    case 77:
    case 78:
    case 79:
        break;
    case 80: {
        REQUIRE(A, "actor pointer is null");
        float x = (float)(s32)(s16)P2 / 100.0f;
        if ((u16)P1 == 1)
            write_float(A + 0x674, x + read_float(A + 0x674));
        else if ((u16)P1 == 0)
            write_float(A + 0x674, read_float(A + 0x674) - x);
        break;
    }
    case 81:
        break;
    case 82: {
        REQUIRE(A, "actor pointer is null");
        u64 S = call4(0x39b760, 0, 0, 0, 0);
        if (!((rd32(A + 0x6c8) >> 7) & 1))
            break;
        REQUIRE(S, "39b760 returned null (the DLL dereferences it unchecked)");
        u64 mask = ((u16)P1 == 8) ? 0x100 : 0;
        if (!(rd64(S) & mask))
            break;
        u64 V = rd64(A + 0xdc8);
        call5(0x3bde30, V + 4, 0, 0, 0, 0);
        u64 T = call5(0x3d74e0, A, (u32)(s32)(s16)P1, 0, 0, 0);
        if (T) {
            call5(0x3d78d0, A, T, V + 4, 0, 0);
        }
        break;
    }
    case 83: {
        REQUIRE(A, "actor pointer is null");
        u64 S = call4(0x39b760, 0, 0, 0, 0);
        REQUIRE(S, "39b760 returned null (the DLL dereferences it unchecked)");
        u32 mask;
        switch ((u16)P1) {
        case 0:
            mask = 1;
            break;
        case 1:
            mask = 2;
            break;
        case 2:
            mask = 4;
            break;
        case 4:
            mask = 0x10;
            break;
        case 5:
            mask = 0x20;
            break;
        case 6:
            mask = 0x40;
            break;
        case 7:
            mask = 0x80;
            break;
        case 8:
            mask = 0x100;
            break;
        case 9:
            mask = 0x200;
            break;
        case 10:
            mask = 0x400;
            break;
        case 11:
            mask = 0x800;
            break;
        case 12:
            mask = 0x1000;
            break;
        case 15:
            mask = 0x8000;
            break;
        case 16:
            mask = 0x10000;
            break;
        case 17:
            mask = 0x20000;
            break;
        case 18:
            mask = 0x40000;
            break;
        case 19:
            mask = 0x80000;
            break;
        case 24:
            mask = 0x1000000;
            break;
        case 25:
            mask = 0x2000000;
            break;
        default:
            mask = 0;
        }
        call5(0x3b3e80, 0, 0, 0, 0, 0);
        if (rd64(S + 8) & (u64)(s64)(s32)mask) {
            call5(0x3cb9f0, 0, A, 1, 0, 0);
        }
        break;
    }
    default:
        break;
    }
}

static void frame_extension(Mp *m, s32 op) {
    u64 R = m->R, C = m->C, A = m->A;
    s32 X = (s16)rd16(R + 4), Y = (s16)rd16(R + 6);
    switch (op) {
    case 50:
        REQUIRE(A, "actor pointer is null");
        call5(0x3b4da0, A, (u32)X, 0, 0, 0);
        break;
    case 51:
        REQUIRE(A, "actor pointer is null");
        wr64(A + 0x20, 0);
        wr64(A + 0x28, 0);
        break;
    case 52:
        REQUIRE(A, "actor pointer is null");
        wr32(A + 0x134, 0x42c60000u);
        break;
    case 53: {
        REQUIRE(A, "actor pointer is null");
        u64 T = rd64(A + 0x148);
        wr32(A + 0x134, T ? rd32(T + 0xc) : 0x3e860a92u);
        break;
    }
    case 54:
        REQUIRE(A, "actor pointer is null");
        write_float(A + 0x138, read_float(A + 0x138) + 8.0f);
        break;
    case 55:
        REQUIRE(A, "actor pointer is null");
        wr32(A + 0x138, 0x41800000u);
        break;
    case 56:
        break;
    case 57:
        motion_call(0x3c6dc0, C, (u32)X, (float)Y / 100.0f, 0.0f, 0);
        break;
    case 58:
        motion_call(0x3c8310, C, (u32)X, (float)Y / 100.0f, 0.0f, 0);
        break;
    case 59: {
        REQUIRE(A, "actor pointer is null");
        static const u16 off[7] = {0x128, 0x12c, 0x130, 0x134, 0x138, 0x13c, 0x140};
        if ((u32)X <= 6) {
            write_float(A + off[X], (float)Y / 100.0f);
        }
        break;
    }
    case 60: {
        REQUIRE(A, "actor pointer is null");
        u64 T = rd64(A + 0x148);
        REQUIRE(T, "A[+0x148] is null");
        write_float(T + 0x2c, (float)Y / 100.0f);
        break;
    }
    case 61: {
        if ((u32)X <= 1) {
            u64 obj = rd64(C + 0xd8 + 8 * (u64)X);
            REQUIRE(A, "actor pointer is null");
            call5(0x3d9fe0, obj + 0x2380, A, (u32)Y, 0, 0);
        }
        break;
    }
    case 62: {
        REQUIRE(A, "actor pointer is null");
        u64 q918 = rd64(A + 0x918);
        REQUIRE(q918, "A[+0x918] is null");
        u64 T = call5(0x3eaae0, (u32)(s32)(s16)rd16(q918 + 0x4e), 0, 0, 0, 0);
        if ((u32)X <= 1) {
            REQUIRE(T, "3eaae0 returned null (the DLL dereferences it unchecked)");
            u64 obj = rd64(C + 0xd8 + 8 * (u64)X);
            call5(0x3d9fe0, obj + 0x2380, A, (u32)(s32)(s16)rd16(T + 4 + 2 * (u64)X), 0, 0);
        }
        break;
    }
    case 63:
    case 64: {
        REQUIRE(A, "actor pointer is null");
        u64 sheet = rd64(A + 0x5c0);
        if (sheet) {
            wr32(sheet + 8, op == 63 ? (u32)X : 0);
        }
        break;
    }
    case 65: {
        REQUIRE(A, "actor pointer is null");
        char tmp[16];
        snprintf(tmp, sizeof tmp, "%04d", (int)X);
        u8 five[5] = {0};
        memcpy(five, tmp, strlen(tmp) < 4 ? strlen(tmp) : 4);
        u64 buf = (u64)five;

        call5(0x3db2f0, A, buf, A, 0, 0);
        break;
    }
    case 66:
    case 67: {
        REQUIRE(A, "actor pointer is null");
        u64 q7b0 = rd64(A + 0x7b0);
        REQUIRE(q7b0, "A[+0x7b0] is null (callee dereferences it)");
        call5(0x1a85e0, q7b0, (u32)X, 0, 0, 0);
        break;
    }
    case 68:
    case 69:
        break;
    case 70:
        REQUIRE(A, "actor pointer is null");
        ((void (*)(double, u64, float))rmc_site(0x409b00))(30.0, A, m->lower_bound);
        break;
    case 71: {
        REQUIRE(A, "actor pointer is null");
        u64 sheet = rd64(A + 0x5c0);
        REQUIRE(sheet, "actor sheet A[+0x5c0] is null");
        if (!(read_float(sheet + 0x1c0) > 0.0f)) {
            call5(0x3c0cd0, sheet, (u32)(-X), 0, 0, 0);
        }
        break;
    }
    case 72:
        break;
    case 73: {
        REQUIRE(A, "actor pointer is null");
        u64 sheet = rd64(A + 0x5c0);
        REQUIRE(sheet, "actor sheet A[+0x5c0] is null");
        if (!(read_float(sheet + 0x1c0) > 0.0f)) {
            u64 T = call5(0x3b68b0, (u32)X, 0, 0, 0, 0);
            REQUIRE(T, "3b68b0 returned null (the DLL dereferences it unchecked)");
            call5(0x3c0cd0, sheet, (u32)(-(s32)(s8)rd8(T + 0x1c)), 0, 0, 0);
        }
        break;
    }
    case 74:
    case 75:
    case 76:
    case 77:
    case 78:
    case 79:
        break;
    case 80: {
        REQUIRE(A, "actor pointer is null");
        if (rd8(A + 0x6c8) & 0x80) {
            u64 V = rd64(A + 0xdc8);
            call5(0x3bde30, V + 4, 0, 0, 0, 0);
            u64 T = call5(0x3d74e0, A, (u32)X, 0, 0, 0);
            if (T) {
                call5(0x3d78d0, A, T, V + 4, 0, 0);
            }
        }
        break;
    }
    default:
        break;
    }
}

typedef u64 (*Motion)(u64, u64, float);
static void *range_orig, *frame_orig;
static u64 range_hook(u64 record, u64 context, float bound) {
    int kind = (s8)rd8(record + 4);
    if (kind >= 54 && kind <= 83) {
        Mp m = {record, context, rd64(context + 0x10), bound};
        range_extension(&m, kind);
    }
    return ((Motion)range_orig)(record, context, bound);
}
static u64 frame_hook(u64 record, u64 context, float bound) {
    int kind = (s8)rd8(record + 2);
    if (kind >= 50 && kind <= 80) {
        Mp m = {record, context, rd64(context + 0x10), bound};
        frame_extension(&m, kind);
    }
    return ((Motion)frame_orig)(record, context, bound);
}
void tweaks_motion_plan(void) {
    rmc_hook(0x40d810, range_hook, &range_orig);
    rmc_hook(0x40dd70, frame_hook, &frame_orig);
}
