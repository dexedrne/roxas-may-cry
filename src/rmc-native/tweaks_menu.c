#include "campaign.h"
#include "tweaks_game.h"
static uint16_t command_lists[16];
static char effect_names[20][16], common_names[5][16];
#define NAME_EFFECT(n) ((uintptr_t)effect_names[(n) - 1])
#define NAME_COMMON(k) ((uintptr_t)common_names[k])
static _Noreturn void runtime_fault(const char *reason) {
    fprintf(stderr, "rmc: %s\n", reason);
    abort();
}
static uintptr_t need(uintptr_t p, const char *what) {
    if (!p)
        runtime_fault(what);
    return p;
}
static void *handle_extended_menu_original;
static void *item_menu_category_original;
static void *load_common_scripts_original;
static void *can_use_command_item_original;
static void *record_source_actor_original;
static void *release_effects_original;
static void *start_effect_scripts_original;
static void *probe_effects_original;
static void *build_swim_commands_original;
static u32 first_command(u64 self) {
    if (!(u8)call4(0x39a7e0, 0, 0, 0, 0))
        return 0x68;
    if (rd8(data_at(0x717008)) == 9)
        return 0x88;
    u64 obj = need(rd64(self + 0xae0), "[this+0xae0]");
    if (rd32(obj + 0xde0) == 2)
        return 0x16d;
    return (u8)call4(0x3d7820, obj, 0, 0, 0) ? 0x21d : 1;
}

static void rows_3_to_6(u64 self, u64 list) {
    call4(0x3b11d0, list, 0, 0, 0);
    for (u32 i = 3; i < 7; i++) {
        u64 e = need(call4(0x3b2660, self, i, 0, 0), "the 0x3b2660 row (null when its id word is 0)");
        call4(0x3b0f20, list, (u32)(s32)(s16)rd16(e + 2), e + 0xc, (u32)(s32)(s8)rd8(e + 4));
    }
}

static void menu_main(u64 self) {
    u64 list = self + 0x28;
    if ((u8)call4(0x3b27e0, self, 0, 0, 0)) {
        rows_3_to_6(self, list);
        return;
    }
    call4(0x3b11d0, list, 0, 0, 0);
    u32 id = first_command(self);
    call4(0x3b0f20, list, id, 0, 1);
    u64 obj = need(rd64(self + 0xae0), "[this+0xae0]");
    if (rd32(obj + 0xde0) == 3) {
        call4(0x3b11d0, self + 0x628, 0, 0, 0);
        obj = need(rd64(self + 0xae0), "[this+0xae0]");
        u64 sheet = need(rd64(obj + 0x5c0), "[[this+0xae0]+0x5c0]");
        u8 f = rd8(sheet + 0x1e7);
        u64 L = ((u64)command_lists) + 0x10;
        u32 n = 0;
        memset((void *)L, 0, 16);
        if (f & 0x04) {
            wr16(L, 0x2ba);
            n = 1;
        }
        if (f & 0x08) {
            wr16(L + 2 * (u64)n, 0x2bd);
            n++;
        }
        if (f & 0x10) {
            wr16(L + 2 * (u64)n, 0x2c0);
            n++;
        }
        if (f & 0x20)
            wr16(L + 2 * (u64)n, 0x2ab);

        call4(0x3b1840, self + 0x628, L, 0, 0);
        id = 0x2c3;
    } else
        id = rd16(self + 0xb68) ? 2 : 0x68;
    call4(0x3b0f20, list, id, 0, 1);
    id = (u8)call4(0x3b27c0, self, 0, 0, 0) ? 0x68 : 3;
    call4(0x3b0f20, list, id, 0, 1);
    if (rd8(self + 0x24) & 0x20) {
        call4(0x3b0f20, list, 0x68, 0, 1);
        return;
    }
    obj = rd64(self + 0xae0);
    if ((u8)call4(0x3aa4f0, obj, 0, 0, 0)) {
        u64 e = call4(0x3b0f20, list, 5, 0, 1);
        obj = need(rd64(self + 0xae0), "[this+0xae0]");
        if (rd32(obj + 0xde0) == 6 && (u8)call4(0x3abb20, obj, 0, 0, 0))
            wr8(need(e, "the row returned by 0x3b0f20") + 5, 5);
    } else {
        u64 e = call4(0x3b0f20, list, 4, 0, 1);
        obj = need(rd64(self + 0xae0), "[this+0xae0]");
        if (rd32(obj + 0xde0) != 0)
            wr8(need(e, "the row returned by 0x3b0f20") + 5, 5);
    }
}

static void menu_sub(u64 self) {
    u64 list = self + 0x328;
    if ((u8)call4(0x3b27e0, self, 0, 0, 0)) {
        rows_3_to_6(self, list);
        return;
    }
    call4(0x3b11d0, list, 0, 0, 0);
    u32 id = first_command(self);
    call4(0x3b0f20, list, id, 0, 1);
    if ((u32)call4(0x3de250, 0, 0, 0, 0) != 0)
        id = 0x13c;
    else
        id = ((s32)rd32(self + 0x3b8) <= 0) ? 0x68 : 0x48;
    call4(0x3b0f20, list, id, 0, 1);
    if ((u32)call4(0x3de250, 0, 0, 0, 0) == 2 && (u32)call4(0x3de130, 0, 0, 0, 0) == 0xc9) {
        call4(0x3f3230, self, 0, 0, 0);
        id = 0x25d;
    } else if (rd32(data_at(0x749804)) & 0x2000000u)
        id = 0x68;
    else
        id = (u8)call4(0x39a800, 0, 0, 0, 0) ? 0x49 : 0x68;
    call4(0x3b0f20, list, id, 0, 1);
    id = (u8)call4(0x39a870, 0, 0, 0, 0) ? 0x51 : 0x68;
    call4(0x3b0f20, list, id, 0, 1);
}

static u64 build_field_commands(u64 a) {
    u64 self = a;
    u64 pending = rd64(self + 0xbd8);
    if (pending) {
        return call4(0x3b19b0, self + 0x528, pending, rd64(self + 0xbe0), 0);
    }
    u64 obj = need(rd64(self + 0xae0), "[this+0xae0]");
    if (!rd64(obj + 0x5c0)) {
        return 0;
    }
    u64 Lx = ((u64)command_lists);
    call4(0x3b11d0, self + 0x228, 0, 0, 0);
    for (u32 i = 0; i < 3; i++) {
        u64 m = call4(0x3d5c50, i, 0, 0, 0);
        if (!(u8)call4(0x3ba720, m, 0, 0, 0))
            continue;
        need(m, "the party member returned by 0x3d5c50");
        if (rd32(m + 0x6c8) & 0x400000u)
            continue;
        if (!rd64(m + 0x5c0))
            continue;
        wr64(self + 0xb80 + 8 * (u64)(s64)(s32)rd32(self + 0x238), m);
        u64 ent = need(call4(0x3dfeb0, rd32(m + 0x914), 0, 0, 0), "the 0x3dfeb0 record of [member+0x914]");
        u32 cmd = (u32)call4(0x3e60f0, (u32)(s32)(s16)rd16(ent + 0x4c), 0, 0, 0);
        call4(0x3b0f20, self + 0x228, cmd, 0, 1);
    }
    call4(0x3f3000, self, 0, 0, 0);
    call4(0x3b11d0, self + 0xa8, 0, 0, 0);
    call4(0x3b1840, self + 0xa8, self + 0xb68, 0, 0);
    if (rd16(Lx)) {
        call4(0x3b0f20, self + 0xa8, 0x3e7, 0, 1);
        call4(0x3b11d0, self + 0x6a8, 0, 0, 0);
        call4(0x3b1840, self + 0x6a8, Lx, 0, 0);
    }
    call4(0x3f3300, self, 0, 0, 0);
    call4(0x3f3110, self, 0, 0, 0);
    call4(0x3e7b30, self + 0x4a8, 0, 0, 0);
    if ((u8)call4(0x3bfb30, obj, 0, 0, 0))
        call4(0x3e6d10, self + 0xb76, 0, 0, 0);
    call4(0x3b11d0, self + 0x3a8, 0, 0, 0);
    call4(0x3b1840, self + 0x3a8, self + 0xb76, 0, 0);
    menu_main(self);
    menu_sub(self);
    call4(0x3b11d0, self + 0x2a8, 0, 0, 0);
    u64 save = need(call4(0x152380, 0, 0, 0, 0), "the save block returned by 0x152380");
    for (u32 k = 0; k < 4; k++) {
        s32 id = (s16)rd16(save + 0x36f8 + 2 * (u64)k);
        u64 cmd = need(call4(0x3b68b0, (u32)id, 0, 0, 0), "the command record returned by 0x3b68b0");
        u64 cur = rd64(self + 0xae0);
        u32 flag;
        if (rd16(cmd + 2) == 3) {
            flag = 0;
            u64 tab = need(call4(0x3d74c0, cur, 0, 0, 0), "the ability table returned by 0x3d74c0");
            s32 n = (s8)rd8(tab + 0x12);
            if (n > 0) {
                u16 want = rd16(cmd + 4);
                for (s32 j = 0; j < n; j++)
                    if (rd16(tab + 0x34 + 2 * (u64)j) == want)
                        flag++;
            }
        } else {
            need(cur, "[this+0xae0]");
            if (rd32(cur + 0xde0) == 3) {
                id = (s16)rd16(Lx + 0x10 + 2 * (u64)k);
            }
            flag = 1;
        }
        u64 e = need(call4(0x3b0f20, self + 0x2a8, (u32)id, 0, flag), "the row returned by 0x3b0f20");
        wr8(e + 6, (u8)(k + 1));
    }

    campaign_command_done(self,obj);
    return 0;
}

static u64 reset_field_commands(u64 a) {
    u64 self = a;
    u64 Lx = ((u64)command_lists);
    call4(0x3b2080, self, 0, 0, 0);
    call4(0x3b1b70, self + 0x28, 0, 0, 0);
    call4(0x3b1b70, self + 0x328, 0, 0, 0);
    wr64(self + 0xae0, 0);
    call4(0x3b20b0, self, 0, 0, 0);
    u32 n = 0;
    for (u32 i = 0; i < 6; i++) {
        u32 v = (u32)call4(0x3c5900, i, 0, 0, 0);
        if (v) {
            wr16(self + 0xb68 + 2 * (u64)n, (u16)v);
            n++;
        }
    }
    wr16(self + 0xb68 + 2 * (u64)n, 0);
    n = 0;
    for (u32 i = 6; i < 14; i++) {
        u32 v = (u32)call4(0x3c5900, i, 0, 0, 0);
        if (v) {
            wr16(Lx + 2 * (u64)n, (u16)v);
            n++;
        }
    }
    if (2 * (u64)n >= 0x10)
        runtime_fault("extended drive list lacks terminator space");
    wr16(Lx + 2 * (u64)n, 0);

    u64 save = need(call4(0x152380, 0, 0, 0, 0), "the save block returned by 0x152380");
    for (u32 k = 0; k < 4; k++) {
        u64 slot = save + 0x36f8 + 2 * (u64)k;
        u64 cmd = need(call4(0x3b68b0, (u32)(s32)(s16)rd16(slot), 0, 0, 0),
                       "the command record returned by 0x3b68b0");
        if (rd16(cmd + 2) == 2)
            wr16(slot, (u16)call4(0x3c5900, (u32)(s32)(s16)rd16(cmd + 4), 0, 0, 0));
    }
    call4(0x3e6d10, self + 0xb76, 0, 0, 0);
    wr64(self + 0xbd8, 0);
    wr32(self + 0xbe4, 0);
    return 0;
}

static u64 can_use_command_item(u64 a, u64 b_arg, u64 c_arg, u64 d) {
    u32 id = (u32)a;
    u64 rdx = b_arg, r8 = c_arg, r9 = d;
    u64 item = need(call4(0x3e39a0, id, 0, 0, 0), "the item record returned by 0x3e39a0");
    s32 cmd = (s16)rd16(item + 0x10);
    if ((u16)cmd) {
        u64 rec = need(call4(0x3b68b0, (u32)cmd, 0, 0, 0), "the command record returned by 0x3b68b0");
        if (rd8(rec + 6) == 0xb) {
            return (rec & ~0xffull) | 1;
        }
    }
    return ((Fn4)can_use_command_item_original)(id, rdx, r8, r9);
}

static u64 can_select_item(u64 a) {
    s64 row = (s32)a;
    u64 menu = call4(0x304df0, 0, 0, 0, 0);
    u64 n = call4(0x34bc50, menu, 0, 0, 0);
    if (!(u32)n) {
        return n & ~0xffull;
    }
    u64 list = call4(0x34bc40, menu, 0, 0, 0);
    u64 at = list + 6 * (u64)row;
    need(at, "the item-menu row");
    need(at + 1, "the item-menu row");
    u32 id = rd16(at);
    u64 item = call4(0x3e39a0, id, 0, 0, 0);
    if (id == 0x112 || id == 0x113) {
        return (item & ~0xffull) | 1;
    }
    need(item, "the item record returned by 0x3e39a0");
    u32 k = (u32)rd8(item + 5) - 4;
    static const u8 no[0x12] = {0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0};
    if (k > 0x11)
        return k & 0xffffff00u;
    return no[k] ? 0 : 1;
}

static u64 item_menu_category(u64 a, u64 b_arg, u64 c_arg, u64 d) {
    u32 id = (u32)a;
    u64 rdx = b_arg, r8 = c_arg, r9 = d;
    u64 item = need(call4(0x3e39a0, id, 0, 0, 0), "the item record returned by 0x3e39a0");
    u32 k = (u32)rd8(item + 5) - 1;
    static const u8 sel[0x18] = {0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 2, 3, 4, 5, 1, 1, 6, 0, 0};
    static const u32 val[6] = {0, 5, 1, 2, 3, 4};
    if (k <= 0x17 && sel[k] != 6) {
        return val[sel[k]];
    }
    return ((Fn4)item_menu_category_original)(id, rdx, r8, r9);
}

static u64 build_swim_commands(u64 a, u64 b_arg, u64 c_arg, u64 d) {
    u64 self = a, who = b_arg, list = c_arg, r9 = d;
    u64 r = call4(0x3ba720, who, 0, 0, 0);
    if (!(u8)r) {
        return r;
    }
    need(self, "this");
    if (rd32(self) == 0xa) {
        return (r & ~0xffull) | 1;
    }
    call4(0x3b11d0, list, 0, 0, 0);

    return ((Fn4)build_swim_commands_original)(self, who, list, r9);
}

static u64 record_source_actor(u64 a, u64 b_arg, u64 c_arg, u64 d) {
    u64 self = need(a, "this"), b = b_arg, c = c_arg, r9 = d;
    u64 inner = rd64(self);
    u32 id = (u32)call4(0x4ad240, b, 0, 0, 0);
    wr32(need(inner, "[this]") + 0x2c, id);
    return ((Fn4)record_source_actor_original)(self, b, c, r9);
}

static void extra_found(u64 name) {
    fprintf(stderr, "rmc: custom numbered resource %s has no registered loader\n", (const char *)name);
    abort();
}
static u64 load_common_scripts(u64 a, u64 b_arg, u64 c_arg, u64 d) {
    u64 rcx = a, rdx = b_arg, r8 = c_arg, r9 = d;

    for (int k = 0; k < 5; k++) {
        if (call4(0x3f95f0, NAME_COMMON(k), 0, 0, 0))
            extra_found(NAME_COMMON(k));
    }
    return ((Fn4)load_common_scripts_original)(rcx, rdx, r8, r9);
}
static u64 probe_effects(u64 a, u64 b_arg, u64 c_arg, u64 d) {
    u64 rcx = a, rdx = b_arg, r8 = c_arg, r9 = d;

    for (int n = 1; n <= 20; n++) {
        if ((u32)call4(0x3f95f0, NAME_EFFECT(n), 0, 0, 0))
            extra_found(NAME_EFFECT(n));
    }
    return ((Fn4)probe_effects_original)(rcx, rdx, r8, r9);
}
static u64 start_effect_scripts(u64 a, u64 b_arg, u64 c_arg, u64 d) {
    u64 rcx = a, rdx = b_arg, r8 = c_arg, r9 = d;

    u64 script = call4(0x3e1730, rd64(data_at(0x2a0f688)), 0, 0, 0);
    if (!script)
        runtime_fault("common effect script allocation failed");

    call5(0x3e1840, script, 0x66, 0, 0, 0);
    call4(0x3e1410, script, 0, 0, 0);
    call4(0x3e1510, script, 0, 0, 0);
    for (int k = 0; k < 5; k++) {
        if (call4(0x3f95f0, NAME_COMMON(k), 0, 0, 0))
            extra_found(NAME_COMMON(k));
    }

    return ((Fn4)start_effect_scripts_original)(rcx, rdx, r8, r9);
}
static u64 release_effects(u64 a, u64 b_arg, u64 c_arg, u64 d) {
    u64 rcx = a, rdx = b_arg, r8 = c_arg, r9 = d;
    return ((Fn4)release_effects_original)(rcx, rdx, r8, r9);
}

static u8 icon_c8, icon_c9, icon_ca;
static const struct {
    char prefix[4];
    u8 row, head;
} layouts[] = {
    {"bb0", 0xeb, 0xec}, {"tt6", 0xe8, 0xea}, {"he1", 0xee, 0xec}, {"lm0", 0xee, 0xed}, {"wi0", 0xee, 0xea},
    {"lk0", 0xee, 0xeb}, {"hb2", 0xee, 0xec}, {"nm0", 0xee, 0xec}, {"he0", 0xe7, 0xec}, {"wm0", 0xe8, 0xec},
    {"hb0", 0xe8, 0xec}, {"hb1", 0xe8, 0xec}, {"dc0", 0xeb, 0xec}, {"po0", 0xeb, 0xea}, {"tr0", 0xeb, 0xea},
    {"tt0", 0xeb, 0xeb}, {"tt2", 0xeb, 0xea}, {"tt3", 0xeb, 0xea}, {"tt4", 0xeb, 0xea}, {"tt5", 0xeb, 0xea},
    {"ca0", 0xea, 0xeb}, {"mu0", 0xec, 0xea}, {"nm1", 0xed, 0xea}, {"eh0", 0xed, 0xeb}, {"dc1", 0xed, 0xe6},
    {"al0", 0xe6, 0xe6}, {"eh1", 0xe3, 0xe5}, {"tt1", 0xe3, 0xe5}, {"zz0", 0xe3, 0xea},
};
static void layout_icon(u8 *icon, int off, u8 fallback) {
    const char *name = (const char *)data_at(0x9babb0);
    *icon = fallback;
    if (strlen(name) != 14 || strcmp(name + 3, "command.2dd"))
        return;
    for (unsigned i = 0; i < sizeof layouts / sizeof layouts[0]; i++)
        if (!memcmp(name, layouts[i].prefix, 3)) {
            *icon = off == 14 ? layouts[i].head : layouts[i].row;
            return;
        }
}
static u64 row_text(u64 out, u8 icon, u8 colour, u32 msg) {
    need(out, "the text buffer");
    need(out + 0xb, "the text buffer");
    wr8(out + 1, icon);
    wr8(out, 0x11);
    wr32(out + 2, 0x09001cffu);
    wr8(out + 6, colour);
    wr32(out + 7, 0x1c000211u);
    wr8(out + 0xb, 0);
    u64 text = call4(0x3ad9a0, msg, 0, 0, 0);
    call4(0x3ae260, text, out + 0xc, 0, 0);
    return out;
}
static u64 layout_header_icon(void) {
    layout_icon(&icon_ca, 0xe, 0xe2);
    return (u32)(s32)(s8)icon_ca;
}
static u64 layout_command_text(u64 unused0 __attribute__((unused)), u64 b_arg, u64 c_arg, u64 d) {
    u64 out = b_arg;
    u8 colour = (u8)c_arg;
    u32 msg = (u32)d;
    layout_icon(&icon_c9, 0xc, 0xe7);
    return row_text(out, icon_c9, colour, msg);
}
static u64 submenu_command_text(u64 unused0 __attribute__((unused)), u64 b_arg, u64 c_arg, u64 d, u64 e_arg) {
    u64 out = b_arg;
    u8 colour = (u8)c_arg;
    u32 msg = (u32)d;
    u64 list = need(e_arg, "the list (argument 5)");
    u64 owner = rd64(list + 8);
    s32 type = owner ? (s32)rd32(owner + 0xab0) : 0;

    static const u8 fixed[13] = {0, 0xde, 0xdd, 0xe2, 0xe2, 0xe3, 0xdc, 0xdc, 0xe1, 0xde, 0xe2, 0xe4, 0xe0};
    if (type >= 1 && type <= 12)
        icon_c8 = fixed[type];
    else if (type >= 0x64 && type <= 0x6e)
        icon_c8 = 0;

    return row_text(out, icon_c8, colour, msg);
}

static void jump_record(u64 rec, u64 src, int o0, int o1, int o2, int o4, int o6, int o8, int sx) {
    need(src, "the record the jump is built from");
    need(src + 0x38, "the record the jump is built from");
    wr8(rec, rd8(src + o0));
    wr8(rec + 1, rd8(src + o1));
    wr8(rec + 2, rd8(src + o2));
    wr8(rec + 3, 0);
    u8 a = rd8(src + o4), b = rd8(src + o6), c = rd8(src + o8);
    wr16(rec + 4, sx ? (u16)(s16)(s8)a : a);
    wr16(rec + 6, sx ? (u16)(s16)(s8)b : b);
    wr16(rec + 8, sx ? (u16)(s16)(s8)c : c);
}
static u64 handle_extended_menu(u64 a, u64 b_arg, u64 c_arg, u64 d) {
    u64 self = a, rdx = b_arg, r8 = c_arg, r9 = d;
    call4(0x2f5f30, self, 0, 0, 0);
    u64 r = call4(0x2f6230, 0, 0, 0, 0);
    if ((u8)r) {
        r = call4(0x151730, 0, 0, 0, 0);
        return (r & ~0xffull) | 1;
    }
    u64 cur = need(rd64(data_at(0xbef528)), "the menu cursor pointer [0x140bef528]");
    u32 row = rd16(cur);
    u64 ids = need(rd64(data_at(0xbef538)), "the menu row-id list [0x140bef538]");
    need(ids + 2 * (u64)row, "the menu row id");
    s32 id = (s16)rd16(ids + 2 * (u64)row);
    u64 pad = call4(0x39b760, 0, 0, 0, 0);
    if (pad && (rd32(need(pad, "the pad state returned by 0x39b760") + 8) & 0x1000000u) && id >= 0x200 &&
        id <= 0x204) {
        u8 jump[10];
        u64 rec = (u64)jump;

        if (id == 0x203) {
            u64 t = need(call4(0x3ad500, 0, 0, 0, 0), "the record returned by 0x3ad500");
            need(t + row + 0x38, "the record returned by 0x3ad500");
            call4(0x31d850, (u32)(s32)(s8)rd8(t + row + 0x38), 0, 0, 0);
        } else {
            call4(0x2eb790, 0, 0, 0, 0);
            call4(0x1dd170, 2, 0, 0, 0);
            call4(0x2f6500, 0, 0, 0, 0);
            if (id == 0x202) {
                u64 t = need(call4(0x3ad500, 0, 0, 0, 0), "the record returned by 0x3ad500");
                need(t + 0x2b, "the record returned by 0x3ad500");
                call4(0x3aabe0, (u32)((s32)(s8)rd8(t + 0x27) + (s32)row),
                      (u32)((s32)(s8)rd8(t + 0x2b) + (s32)row), 0, 0);
            } else if (id == 0x204) {
                u64 t = call4(0x3ad500, 0, 0, 0, 0);
                jump_record(rec, t, 0x30, 0x31, 0x32, 0x33, 0x35, 0x37, 1);
                call5(0x152990, rec, 1, 1, 1, 0);
            } else {
                u64 w = data_at(0x717008) + (id == 0x201 ? 0x20 : 0);
                jump_record(rec, w, 0, 1, 2, 4, 6, 8, 0);
                call5(0x152990, rec, 1, 1, 1, 0);
            }
        }

        if (id != 0x204) {
            return 0;
        }
    }
    return ((Fn4)handle_extended_menu_original)(self, rdx, r8, r9);
}

void tweaks_menu_plan(void) {
    for (unsigned i = 0; i < 5; i++)
        snprintf(common_names[i], 16, "%02ucommon.bdx", i + 1);
    for (unsigned i = 0; i < 20; i++)
        snprintf(effect_names[i], 16, "%02ueffect.bar", i + 1);
    rmc_hook(0x3f29e0, build_field_commands, NULL);
    rmc_hook(0x3f27d0, reset_field_commands, NULL);
    rmc_hook(0x3c4780, can_use_command_item, &can_use_command_item_original);
    rmc_hook(0x351390, can_select_item, NULL);
    rmc_hook(0x368820, item_menu_category, &item_menu_category_original);
    rmc_hook(0x4060e0, build_swim_commands, &build_swim_commands_original);
    rmc_hook(0x3d1730, record_source_actor, &record_source_actor_original);
    rmc_hook(0x3a17f0, load_common_scripts, &load_common_scripts_original);
    rmc_hook(0x3f6620, probe_effects, &probe_effects_original);
    rmc_hook(0x3f65a0, start_effect_scripts, &start_effect_scripts_original);
    rmc_hook(0x3f6510, release_effects, &release_effects_original);
    rmc_hook(0x153850, layout_header_icon, NULL);
    rmc_hook(0x15aab0, layout_command_text, NULL);
    rmc_hook(0x15aba0, submenu_command_text, NULL);
    rmc_hook(0x31dd70, handle_extended_menu, &handle_extended_menu_original);
}
