#include "rmc.h"
#include "config.h"
#include <stdint.h>
#include <limits.h>
#include <string.h>

void tweaks_actor_plan(void);
void tweaks_menu_plan(void);
void tweaks_motion_plan(void);
void tweaks_optional_plan(void);

struct Edit {
    unsigned site;
    const char *section, *option;
    const char *literal;
    unsigned size_or_offset;
};
#define BYTES(site, section, key, data) {site, section, key, data, sizeof(data) - 1}
#define REL(site, section, key, offset) {site, section, key, NULL, offset}
static const struct Edit edits[] = {
#include "tweaks_edits.inc"
};

struct Symbol {
    uint32_t id;
    uint8_t fields[4];
};
_Static_assert(sizeof(struct Symbol) == 8, "world symbol stride");
static const struct Symbol fixed_symbols[] = {
    {930, {2, 40, 9, 0}}, {931, {4, 42, 0, 0}},  {932, {8, 0, 1, 0}},  {933, {5, 0, 2, 0}},
    {934, {6, 0, 5, 0}},  {935, {12, 50, 0, 0}}, {936, {11, 0, 7, 0}}, {937, {16, 0, 8, 0}},
    {938, {14, 0, 3, 0}}, {939, {7, 0, 4, 0}},   {940, {10, 0, 6, 0}}, {1110, {18, 0, 0, 0}}};
static void write_byte(unsigned site, unsigned value) {
    uint8_t byte = (uint8_t)value;
    rmc_write(site, &byte, sizeof(byte));
}
static void static_edits(void) {
    unsigned char *page = rmc_near(4096);
    if (!page) {
        rmc_fail("world symbol storage allocation");
        return;
    }
    memset(page, 0, 4096);
    float gravity = (float)rmc_value("combat", "blow_gravity", 0);
    memcpy(page, &gravity, sizeof gravity);
    memcpy(page + 16, fixed_symbols, sizeof fixed_symbols);
    static const char *symbols[] = {"symbol_zz", "symbol_es", "symbol_di",
                                    "symbol_po", "symbol_wi", "symbol_tr"};
    for (unsigned i = 0; i < 6; ++i) {
        struct Symbol s = {.id = (uint32_t)rmc_value("worlds", symbols[i], 0)};
        for (unsigned j = 0; j < 4; ++j)
            s.fields[j] = (uint8_t)rmc_value("worlds", symbols[i], j + 1);
        memcpy(page + 0x70 + 8 * i, &s, sizeof s);
    }
    for (unsigned i = 0; i < sizeof edits / sizeof edits[0]; ++i) {
        const struct Edit *e = &edits[i];
        if (e->option && !rmc_int(e->section, e->option))
            continue;
        if (e->literal)
            rmc_write(e->site, e->literal, e->size_or_offset);
        else {
            intptr_t delta = (intptr_t)(page + e->size_or_offset) - (intptr_t)(rmc_site(e->site) + 4);
            if (delta < INT32_MIN || delta > INT32_MAX) {
                rmc_fail("world symbol reference outside rel32 range");
                return;
            }
            int32_t rel = (int32_t)delta;
            rmc_write(e->site, &rel, sizeof rel);
        }
    }

    static const char *growth[] = {"valor_growth", "wisdom_growth", "limit_growth", "master_growth",
                                   "final_growth"};
    uint16_t table[5][5];
    for (unsigned i = 0; i < 5; i++)
        for (unsigned j = 0; j < 5; j++)
            table[i][j] = (uint16_t)rmc_value("forms", growth[i], j);
    rmc_write(0x5c9030, table, sizeof table);
    float camera[2][6];
    for (unsigned j = 0; j < 6; j++) {
        camera[0][j] = (float)rmc_value("camera", "camera_patches_outdoors", j);
        camera[1][j] = (float)rmc_value("camera", "camera_patches_indoors", j);
    }
    rmc_write(0x718c00, camera, sizeof camera);
    /* The game CRT reloads this first indoor value from a second location. */
    if (camera[1][0] != 1.05f)
        rmc_write(0x718bdc, &camera[1][0], sizeof(float));
    gravity = (float)rmc_value("combat", "global_gravity", 0);
    rmc_write(0x5ce184, &gravity, sizeof gravity);
    write_byte(0x3a14bc, rmc_int("levelcap", "set_level_cap"));
    write_byte(0x3ed0bc, rmc_int("levelcap", "set_level_cap"));
    write_byte(0x3f205e, rmc_int("worlds", "worldpoint_amount"));
    uint32_t cursor_mask = 0;
    for (unsigned i = 0; i < 19; i++) {
        unsigned bit = (unsigned)rmc_value("combat", "CursorRemain", i);
        if (bit > 0 && bit < 32)
            cursor_mask |= 1u << bit;
    }
    if (!cursor_mask)
        cursor_mask = 0x7c420;
    rmc_write(0x3b1788, &cursor_mask, sizeof cursor_mask);
    if (rmc_int("antiform", "antiform_tweaks")) {
        static const unsigned refs[] = {0x3a5719, 0x3c0b97, 0x3dbf6f, 0x3f2ed2, 0x404d06, 0x4050fb, 0x40870a};
        for (unsigned i = 0; i < 7; i++)
            write_byte(refs[i], 99);
    }
    if (rmc_int("antiform", "disable_ability_anti"))
        write_byte(0x421e04, 0);
    if (rmc_int("antiform", "disable_random_anti"))
        rmc_write(0x421e16, "\x45\x6b\xc0\x00", 4);
    if (rmc_int("worlds", "halve_gummiship_plans"))
        write_byte(0x27a84c, 9);
    if (rmc_int("worlds", "halve_tinyship_plans"))
        write_byte(0x27a8dc, 9);
}
void tweaks_plan(void) {
    static_edits();
    tweaks_actor_plan();
    tweaks_menu_plan();
    tweaks_motion_plan();
    tweaks_optional_plan();
}
