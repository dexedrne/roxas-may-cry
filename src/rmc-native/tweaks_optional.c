#include "campaign.h"
#include "tweaks_game.h"
static int kh_retry, kh_gummiskip, shortcuts;
static u8 checkpoint[0x10fc0];
static int keep_inventory, keep_party, restore_party;
#define RETRY_BACKUP data_at(0x29fb580)
static _Noreturn void fail_game(const char *what) {
    fputs(what, stderr);
    abort();
}
static void *capture_checkpoint_original;
static void *unlock_routes_original;
static void *reset_retry_original;
static void *restore_retry_original;
static void *build_retry_menu_original;
static void *select_retry_original;
static u64 recover_party(u64 a) {
    u64 self = a, party = call4(0x3e2e20, UINT32_MAX, 0, 0, 0);
    for (int i = 0; i < 4; i++) {
        u32 member = call4(0x3e3680, party, i, 0, 0);
        if (!member)
            continue;
        u64 s = call4(0x3c04c0, member, UINT32_MAX, 0, 0);
        if (!s)
            fail_game("savepoint member lacks status sheet");

        s = campaign_recovery_sheet(s);
        if (rd32(s + 0x268)) {
            float recharge;
            memcpy(&recharge, (void *)(s + 0x1c0), 4);
            if ((s32)rd32(s) < (s32)rd32(s + 4) || recharge > 0 ||
                (s32)rd32(s + 0x180) < (s32)rd32(s + 0x184) ||
                (rd8(s + 0x1b1) < rd8(s + 0x1b2) && rd8(s + 0x1b0) != 100)) {
                call4(0x3c0c30, s, 1, 0, 0);
                call4(0x3c0cd0, s, 1, 0, 0);
                call4(0x3c0ad0, s, 2, 0, 0);
                wr8(self + 0xc08, 1);
            }
        } else {
            call4(0x3c2040, s, 0, 0, 0);
            call4(0x3c0860, s, rd32(s + 4), 0, 0);
        }
        call4(0x3c07e0, s, 0, 0, 0);
    }
    if (rd8(self + 0xc09) == 1)
        wr16(self + 0xc08, 1);
    return 0;
}

static u64 capture_checkpoint(u64 a, u64 b, u64 c, u64 d) {
    if (kh_retry && (u8)call4(0x3a0540, 0, 0, 0, 0)) {
        u64 save = call4(0x152380, 0, 0, 0, 0);
        memcpy(checkpoint, (void *)(save), sizeof checkpoint);
    }
    return ((Fn4)capture_checkpoint_original)(a, b, c, d);
}
static u64 build_retry_menu(u64 a, u64 b, u64 c, u64 d) {
    if (kh_retry) {
        int retry = (u32)call4(0x3aba30, 0, 0, 0, 0) == 2;
        u64 menu = call4(0x3ad480, 1, 0, 0, 0);
        wr16(menu + 2, retry ? 0x204 : 2);
        wr32(menu + 12, retry ? 0x8ab100ff : 0);
        wr32(menu + 16, retry ? 0xd62e0100 : 0);
    }
    return ((Fn4)build_retry_menu_original)(a, b, c, d);
}
static u64 select_retry(u64 a, u64 b, u64 c, u64 d) {
    if (kh_retry) {
        u32 cmd = a;
        if (cmd == 2)
            memcpy((void *)(RETRY_BACKUP), checkpoint, sizeof checkpoint);
        if (cmd == 0xff || cmd == 0x100) {
            u8 bytes[10];
            u64 w = data_at(0x717008), rec = (u64)bytes;

            wr32(rec, rd32(w) & 0xffffff);
            for (int off = 4; off <= 8; off += 2)
                wr16(rec + off, rd8(w + off));
            call5(0x152990, rec, 0x80000001, 1, 1, 0);

            if (cmd == 0xff)
                keep_inventory = 1;
            else
                keep_party = 1;
        }
        if (cmd == 0x101)
            wr8(data_at(0xabac5a), 1);
    }
    return ((Fn4)select_retry_original)(a, b, c, d);
}

static void retry_frame(void) {
    if (!restore_party || (u8)call4(0x1515c0, 0, 0, 0, 0))
        return;
    u64 save = call4(0x152380, 0, 0, 0, 0);
    const struct {
        u32 offset, size;
    } parts[] = {{0x24f0, 0x1040}, {0x3580, 0x140}, {0x36f8, 8}, {0xe000, 0x28}};
    for (unsigned i = 0; i < 4; i++)
        memcpy((void *)(RETRY_BACKUP + parts[i].offset), (void *)(save + parts[i].offset), parts[i].size);
    restore_party = 0;
}
static u64 restore_retry(u64 a, u64 b, u64 c, u64 d) {
    if (kh_retry) {
        u64 save = call4(0x152380, 0, 0, 0, 0);
        if (keep_inventory)
            memcpy((void *)(RETRY_BACKUP + 16), (void *)(save + 16), 0x1c80);
        if (keep_party) {
            call4(0x2e4950, 0, UINT32_MAX, 0, 0);
            restore_party = 1;
        }
        if ((u32)a == 0xb9) {
            call4(0x3a1600, save + 0x24f0, 1, 0x29, 0);
            return 0;
        }
    }
    return ((Fn4)restore_retry_original)(a, b, c, d);
}
static u64 reset_retry(u64 a, u64 b, u64 c, u64 d) {
    if (kh_retry)
        keep_inventory = keep_party = 0;
    return ((Fn4)reset_retry_original)(a, b, c, d);
}

static u64 unlock_routes(u64 a, u64 b, u64 c, u64 d) {
    if (kh_gummiskip) {
        u64 save = call4(0x152380, 0, 0, 0, 0);
        for (int i = 0; i < 19; i++)
            wr8(save + 0x3fef + 4 * i, 0);
        wr32(save + 0x4158, 0x02020202);
        wr32(save + 0x415c, 0x02020202);
    }
    return ((Fn4)unlock_routes_original)(a, b, c, d);
}

static void shortcut_frame(void) {
    static unsigned held;
    if (!shortcuts || rd8(data_at(0x7435d0)) != 10)
        return;
    u64 save = call4(0x152380, 0, 0, 0, 0), pad = call4(0x39b760, 0, 0, 0, 0);
    if (!pad)
        return;
    unsigned page = rd8(save + 0xe000), menu = rd8(data_at(0x7435d4)), keys = rd8(pad + 8) & 6;
    if (page > 4)
        fail_game("shortcut page exceeds 4");
    if (keys && !held && (menu == 25 || menu == 26)) {
        unsigned next = page;
        if ((keys & 2) && page < 4)
            next = page + 1;
        if ((keys & 4) && page > 0)
            next = page - 1;
        if (next != page) {
            wr64(save + 0xe001 + page * 8, rd64(save + 0x36f8));
            page = next;
            wr8(save + 0xe000, page);
            wr64(save + 0x36f8, rd64(save + 0xe001 + page * 8));
            call4(0x1dd170, 1, 0, 0, 0);
            u64 selection = rd64(data_at(0xbee660));
            if (menu == 26 || (selection && !rd16(selection)))
                call4(0x35baa0, 0, 0, 0, 0);
        }
    }
    held = keys;
    if (!keys)
        wr64(save + 0xe001 + page * 8, rd64(save + 0x36f8));
    u64 row = call4(0x3ad9a0, 0x451, 0, 0, 0);
    if (row) {
        static const u8 icons[] = {0xee, 0xf1, 0xef, 0xf0, 0xe3};
        wr8(row + 7, 0x91 + page);
        wr8(row + 16, icons[page]);
    }
}

void tweaks_frame(void) {
    retry_frame();
    shortcut_frame();
}
void tweaks_optional_plan(void) {
    if (rmc_int("savepoint", "savepoint_qol"))
        rmc_hook(0x418f30, recover_party, NULL);
    kh_retry = rmc_int("retry", "retry");
    if (kh_retry) {
        rmc_hook(0x3a0660, capture_checkpoint, &capture_checkpoint_original);
        rmc_hook(0x3fc2d0, reset_retry, &reset_retry_original);
        rmc_hook(0x3fc350, restore_retry, &restore_retry_original);
        rmc_hook(0x3fc7a0, build_retry_menu, &build_retry_menu_original);
        rmc_hook(0x3fcbe0, select_retry, &select_retry_original);
    }
    kh_gummiskip = rmc_int("worlds", "patch_gummiskip");
    if (kh_gummiskip)
        rmc_hook(0x3f1e20, unlock_routes, &unlock_routes_original);
    shortcuts = rmc_int("multipleshortcuts", "multiple_shortcuts");
}
