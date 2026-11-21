#include "look.h"
#include "campaign.h"
#include "tweaks_game.h"

enum { LEVEL = 0x2498, PORTRAIT = 0x419c };
static const u32 tag[] = {0, 0, 0x584b4200, 0x58414400};
static int modes;
static const u32 original[] = {0xc331, 0xc332, 0xc333, 0xce33, 0xc334, 0xc335, 0xc336, 0xce34};
static const char *const labels[] = {"Beginner",          "Standard",        "Critical",
                                     "Less Than Nothing", "Xion Knows Best", "Xion Always Cries"};
static const char *const titles[] = {"Critical",
                                     "Xion Knows Best",
                                     "Xion Always Cries",
                                     "Play as Xion on Critical.",
                                     "Roxas steps in",
                                     "Xion returns",
                                     "Play as Xion on Less Than Nothing.",
                                     "Less Than Nothing"};
static u8 label_text[6][40], menu_text[8][40];
static void encode(u8 *dst, const char *s, int compact) {
    if (compact) {
        *dst++ = 11;
        *dst++ = 76;
    }
    for (; *s; ++s) {
        if (*s >= 'a' && *s <= 'z')
            *dst++ = 0x9a + *s - 'a';
        else if (*s >= 'A' && *s <= 'Z')
            *dst++ = 0x2e + *s - 'A';
        else
            *dst++ = *s == '.' ? 0x4f : 1;
    }
    *dst = 0;
}
const u8 *campaign_message(int id) {
    unsigned i = (id & 0x7fff) - 30000u;
    if (i >= 8)
        return NULL;
    if (!modes)
        return (const u8 *)call4(0x3ad9a0, original[i], 0, 0, 0);
    static const u8 text[] = {0, 7, 1, 2, 0, 0, 3, 6};
    if (i == 4 || i == 5)
        return (const u8 *)call4(0x3ad9a0, original[i + 2], 0, 0, 0);
    return menu_text[text[i]];
}
const u8 *campaign_notice(int entering) { return menu_text[entering ? 5 : 4]; }

static int saved_mode(u64 save) {
    unsigned level = rd32(save + LEVEL);
    return level >= 2 && level <= 3 && (rd32(save + PORTRAIT) & ~511u) == tag[level] ? (int)level : 0;
}
int campaign_mode(void) { return saved_mode(data_at(0x9a98b0)); }

typedef u64 (*NoArgs)(void);
static NoArgs init, build, commit;
static u64 menu_init(void) {
    modes = rmc_int("gameplay", "xion_modes");
    u64 result = init();
    if (modes)
        wr32(data_at(0xb1d810), 0);
    return result;
}
static u64 menu_build(void) {
    modes = rmc_int("gameplay", "xion_modes");
    u64 result = build();
    if (modes)
        wr32(data_at(0xb1d810), 0);
    return result;
}
static u64 choose(void) {
    look_reset();
    u64 cursor = data_at(0xb1d810), save = data_at(0x9a98b0);
    if (!modes) {
        if (saved_mode(save))
            wr32(save + PORTRAIT, rd32(save + PORTRAIT) & 511u);
        return commit();
    }
    unsigned row = rd32(cursor);
    if (row >= 4) {
        rmc_fail("Invalid New Game difficulty row");
        return 0;
    }
    unsigned level = 2 + (row & 1);
    wr32(cursor, level);
    /* Stock initialization clears this word. Preserve the portrait index and world flag. */
    wr32(save + PORTRAIT, (rd32(save + PORTRAIT) & 511u) | (row >= 2 ? tag[level] : 0));
    u64 result = commit();
    wr32(cursor, row);
    return result;
}

static void remember(int slot, u64 save) {
    u64 rows = rd64(data_at(0xbee368));
    if (slot < 0)
        slot = (s8)rd8(rows + 1);
    if ((unsigned)slot >= 99)
        return;
    if (!save)
        save = rd64(data_at(0xbee360));
    u64 row = rows + 40 * slot;
    u32 level = rd32(save + LEVEL);
    unsigned label = level < 4 ? 1 + level : 0;
    if (saved_mode(save)) {
        label += 2;
        wr8(row + 0x23, 6);
    }
    wr8(row + 0x25, label);
    if (!label)
        wr8(row + 0x20, 1); /* Native formatter indexes four strings with this byte. */
}
static u64 (*row_written)(int), (*row_read)(int, u64, unsigned), (*format_rows)(int);
static u64 write_row(int slot) {
    u64 result = row_written(slot);
    remember(slot, 0);
    return result;
}
static u64 read_row(int slot, u64 save, unsigned flags) {
    u64 result = row_read(slot, save, flags);
    if (flags & 16)
        remember(slot, save);
    return result;
}
static void add_label(u64 fields, unsigned label) {
    if (!label || label > 6 || rd32(fields) != 0)
        return;
    for (int i = 2; i < 5; ++i) {
        u64 f = fields + 16 * i;
        unsigned kind = rd32(f);
        if (kind == 2) {
            if (i == 2 && rd64(f + 8) && rd32(f + 16) != UINT32_MAX) {
                wr64(f + 8, (u64)label_text[label - 1]);
                return;
            }
            wr32(f, 4);
        } else if (kind == UINT32_MAX) {
            wr32(f, 2);
            wr64(f + 8, (u64)label_text[label - 1]);
            return;
        }
    }
}
static u64 summary(int which) {
    u64 result = format_rows(which);
    if (!rmc_int("gameplay", "save_difficulty"))
        return result;
    u64 list = rd64(data_at(0xbef650)), fields = rd64(data_at(0xbef678)), rows = rd64(data_at(0xbee368));
    int first = (s16)rd16(list + 0x12), total = (s16)rd16(list + 0x16), visible = (s16)rd16(list + 4);
    if (first < 0 || total <= 0 || total > 99 || visible > 4)
        return result;
    for (int i = 0; i < visible; ++i) {
        u64 row = rows + 40 * ((first + i) % total);
        if (rd32(row + 8) & 16)
            add_label(fields + 80 * i, rd8(row + 0x25));
    }
    return result;
}
void campaign_actor_plan(void);
void campaign_plan(void) {
    modes = rmc_int("gameplay", "xion_modes");
    u32 ids[8];
    for (unsigned i = 0; i < 8; i++)
        ids[i] = 0x8000 | (30000 + i);
    if (memcmp(rmc_site(0x5b870c), original, sizeof original))
        rmc_fail("New Game message table mismatch");
    rmc_write(0x5b870c, ids, sizeof ids);
    for (unsigned i = 0; i < 8; ++i)
        encode(menu_text[i], titles[i], 0);
    rmc_hook(0x2b84f0, menu_init, (void **)&init);
    rmc_hook(0x2b8520, menu_build, (void **)&build);
    rmc_hook(0x2b88c0, choose, (void **)&commit);
    campaign_actor_plan();
    for (unsigned i = 0; i < 6; ++i)
        encode(label_text[i], labels[i], i >= 3);
    rmc_hook(0x2ee350, write_row, (void **)&row_written);
    rmc_hook(0x2ee4a0, read_row, (void **)&row_read);
    rmc_hook(0x323250, summary, (void **)&format_rows);
}
