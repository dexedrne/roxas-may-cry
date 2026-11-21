#include "campaign.c"
#include "campaign_actor.c"
#include <assert.h>

static unsigned resets;
void look_reset(void) { ++resets; }
unsigned char *rmc_base;
static int hooks, writes, enable_rows = 1, enable_modes = 1;
static u8 slots[4][0x10fc0], rows[4 * 40], list[32], fields[4 * 80], original_msg;
static u32 expected_message, committed;
static u64 native_message(u64 id, u64 b, u64 c, u64 d) {
    assert(id == expected_message);
    (void)b;
    (void)c;
    (void)d;
    return (u64)&original_msg;
}
static int simulate_field, notices, ko, disposals;
static u64 leader, guest, hud, native_table[24], class_vtable;
static u8 spawn_entry[128], party_entry[128];
static float returned_yaw;
#define NATIVE(fn)                                                                                           \
    static u64 fn(u64 a __attribute__((unused)), u64 b __attribute__((unused)),                              \
                  u64 c __attribute__((unused)), u64 d __attribute__((unused)))
NATIVE(ignore) { return 1; }
NATIVE(none) { return 0; }
NATIVE(get_party) { return (u64)party_entry; }
NATIVE(get_object) {
    assert(a == 792);
    return (u64)spawn_entry;
}
NATIVE(get_cache) {
    assert(a == 792);
    return 4;
}
NATIVE(allocate_guest) {
    assert(a == 0xe60);
    return guest;
}
NATIVE(get_position) { return a + 0x670; }
NATIVE(get_class) {
    assert(a == 123);
    return (u64)&class_vtable;
}
NATIVE(get_id) {
    assert(a == MICKEY);
    return 123;
}
NATIVE(set_hud) {
    assert(a == 0);
    hud = b;
    return 0;
}
NATIVE(get_hud) { return hud; }
NATIVE(notice) {
    assert(a == (u64)campaign_notice(!notices));
    ++notices;
    return 0;
}
NATIVE(park) {
    wr64(PARKED, leader);
    return 0;
}
NATIVE(unpark) {
    wr64(ACTIVE, leader);
    wr64(PARKED, 0);
    wr64(FC + 0xae0, leader);
    return 0;
}
NATIVE(mark_ko) {
    assert(b == leader);
    ko++;
    return 0;
}
NATIVE(dispose) {
    assert(a == guest);
    ++disposals;
    return 0;
}
static void set_yaw(u64 actor, float yaw) {
    assert(actor == leader);
    returned_yaw = yaw;
}
static void construct(u64 a, u64 entry, int mode, u64 parent, u64 position, float angle) {
    assert(a == guest && entry == (u64)spawn_entry && mode == 11 && parent == 0x1234 &&
           position == leader + 0x670 && angle == 1.25f);
    wr64(ACTIVE, a);
    wr64(FC + 0xae0, a);
    wr64(FC + 0xc38, a);
}
unsigned char *rmc_site(unsigned id) {
    if (id == 0x3ad9a0)
        return (void *)native_message;
    if (!simulate_field || id >= 0x500000)
        return rmc_base + id;
    switch (id) {
    case 0x3e2e40:
        return (void *)get_party;
    case 0x3dfeb0:
        return (void *)get_object;
    case 0x3e0160:
        return (void *)get_cache;
    case 0x152430:
        return (void *)allocate_guest;
    case 0x3b5b40:
        return (void *)get_position;
    case 0x4ad270:
        return (void *)get_class;
    case 0x4ad240:
        return (void *)get_id;
    case 0x168850:
        return (void *)set_hud;
    case 0x1689a0:
        return (void *)get_hud;
    case 0x17b710:
        return (void *)notice;
    case 0x3a80e0:
        return (void *)park;
    case 0x3a8140:
        return (void *)unpark;
    case 0x3a7a40:
        return (void *)construct;
    case 0x3b6560:
        return (void *)set_yaw;
    case 0x152570:
        return (void *)dispose;
    case 0x3ad500:
        return (void *)none;
    case 0x3b64d0:
    case 0x4151e0:
    case 0x3bd2e0:
    case 0x3c2120:
    case 0x2e2d60:
        return (void *)ignore;
    default:
        assert(0);
        return NULL;
    }
}
void rmc_fail(const char *m) {
    puts(m);
    abort();
}
void rmc_write_ptr(unsigned char *p, const void *data, size_t len) {
    memcpy(p, data, len);
    ++writes;
}
void rmc_write(unsigned id, const void *data, size_t len) { rmc_write_ptr(rmc_site(id), data, len); }
void rmc_hook(unsigned id, void *fn, void **prev) {
    assert(id && fn && prev);
    ++hooks;
}
int rmc_int(const char *section, const char *key) {
    assert(!strcmp(section, "gameplay"));
    return !strcmp(key, "xion_modes") ? enable_modes : enable_rows;
}
static u64 boot(void) {
    wr32(data_at(0xb1d810), 1);
    return 7;
}
static u64 accept(void) {
    committed = rd32(data_at(0xb1d810));
    wr32(data_at(0x9a98b0) + LEVEL, committed);
    return 8;
}
static u64 native_rows(int style) {
    assert(style == 7);
    return 9;
}
static u64 native_read(int idx, u64 data, unsigned mask) {
    (void)idx;
    (void)data;
    (void)mask;
    return 123;
}
static u64 native_written(int idx) {
    (void)idx;
    return 456;
}
static void menu_and_saves(void) {
    u64 live = data_at(0x9a98b0), cursor = data_at(0xb1d810);
    init_save_original=load_original=ignore;
    assert(init_save(0,0,0,0)==1 && load(0,0,0,0)==1 && load(0,0,0,0)==1 && resets==3);
    init = build = boot;
    commit = accept;
    format_rows = native_rows;
    row_read = native_read;
    row_written = native_written;
    wr64(data_at(0xbee368), (u64)rows);
    wr64(data_at(0xbef650), (u64)list);
    wr64(data_at(0xbef678), (u64)fields);
    assert(menu_init() == 7 && rd32(cursor) == 0 && menu_build() == 7 && rd32(cursor) == 0);
    for (unsigned row = 0; row < 4; ++row) {
        unsigned level = 2 + (row & 1);
        wr32(cursor, row);
        wr32(live + PORTRAIT, 0x107);
        assert(choose() == 8 && resets==4+row && committed == level && rd32(cursor) == row);
        assert(campaign_mode() == (row >= 2 ? (int)level : 0) && (rd32(live + PORTRAIT) & 511) == 0x107);
        memcpy(slots[row], (void *)live, sizeof slots[row]);
        /* Steam 2ed1bc writes the eye byte; 2ed1da replaces just bit 8. */
        slots[row][PORTRAIT] = 4;
        wr32((u64)slots[row] + PORTRAIT, rd32((u64)slots[row] + PORTRAIT) & ~256u);
        memset((void *)live, 0, sizeof slots[row]);
        assert(!campaign_mode());
        memcpy((void *)live, slots[row], sizeof slots[row]);
        assert(campaign_mode() == (row >= 2 ? (int)level : 0));
        assert(read_row(row, (u64)slots[row], 16) == 123 && rows[40 * row + 0x25] == row + 3);
        if (row >= 2)
            assert(rows[40 * row + 0x23] == 6);
    }
    wr64(data_at(0xbee360), (u64)slots[2]);
    rows[1] = 1;
    assert(write_row(-1) == 456 && rows[40 + 0x25] == 5);
    wr32((u64)slots[2] + LEVEL, 3);
    assert(!saved_mode((u64)slots[2]));
    wr32((u64)slots[2] + LEVEL, 258);
    remember(0, (u64)slots[2]);
    assert(!rows[0x25] && rows[0x20] == 1);
    assert(!campaign_message(92) && campaign_message(30003) == menu_text[2]);
    expected_message = 0xc336;
    assert(campaign_message(30004) == &original_msg);
    enable_modes = 0;
    assert(menu_build() == 7 && rd32(cursor) == 1 && campaign_mode() == 3);
    expected_message = 0xc332;
    assert(campaign_message(30001) == &original_msg);
    wr32(cursor, 3);
    assert(choose() == 8 && committed == 3 && !campaign_mode());
    wr32((u64)rows + 8, 16);
    rows[0x25] = 6;
    wr16((u64)list + 4, 1);
    wr16((u64)list + 0x16, 4);
    for (unsigned existing = 0; existing < 2; ++existing)
        for (unsigned place = 0; place < 2; ++place) {
            memset(fields, 0xff, sizeof fields);
            wr32((u64)fields, 0);
            wr32((u64)fields + 16, 1);
            int n = 2;
            if (existing) {
                wr32((u64)fields + 16 * n, 2);
                wr64((u64)fields + 16 * n + 8, 10);
                ++n;
            }
            if (place)
                wr32((u64)fields + 16 * n++, 3);
            u64 time = (u64)fields + 16 * n;
            wr32(time, existing ? 4 : 2);
            wr64(time + 8, 11);
            assert(summary(7) == 9 && rd32(time) == 4 && rd64(time + 8) == 11);
            assert(rd64((existing ? (u64)fields + 32 : time + 16) + 8) == (u64)label_text[5]);
        }
    enable_rows = 0;
    memset(fields, 0xee, sizeof fields);
    assert(summary(7) == 9 && fields[0] == 0xee);
}
static void actors(void) {
    u64 place = VA(0x717008);
    wr16(place, 0x0202);
    wr16(place + 4, 1);
    assert(supported());
    wr16(place, 0x0908);
    wr16(place + 4, 0);
    wr16(place + 6, 10);
    assert(!supported());
    assert(carry_back(2, 2, 7, 20, 60) == 7 && carry_back(1, 2, 7, 20, 60) == 3 && !fraction(0, 20, 60));
    assert(fraction(1, 60, 20) == 1 && fraction(65, 60, 20) == 20);
    u64 r = VA(0xb00000), x = r + 4096, rsh = VA(0x2a17290), xsh = rsh + 0x278;
    wr64(r + 0x5c0, rsh);
    wr64(x + 0x5c0, xsh);
    leader = r;
    guest = x;
    simulate_field = 1;
    wr16((u64)party_entry + 0x24, 792);
    wr16((u64)spawn_entry + 0x4c, 4);
    wr32(VA(0x2a23950), 1);
    wr32(leader, 123);
    wr64(leader + 0xdb8, 0x1234);
    float angle = 1.25f;
    memcpy((void *)(leader + 0x68c), &angle, 4);
    class_vtable = (u64)native_table;
    native_table[0x80 / 8] = native_table[0x88 / 8] = (u64)ignore;
    native_table[0xb0 / 8] = (u64)mark_ko;
    wr32(rsh + 0x260, 1);
    wr32(rsh, 7);
    wr32(rsh + 4, 60);
    wr32(rsh + 0x180, 6);
    wr32(rsh + 0x184, 12);
    wr32(xsh, 4);
    wr32(xsh + 4, 20);
    wr32(xsh + 0x180, 9);
    wr32(xsh + 0x184, 30);
    wr64(ACTIVE, leader);
    wr64(FC + 0xae0, leader);
    wr64(FC + 0xc38, leader);
    wr32(VA(0x9a98b0) + LEVEL, 2);
    wr32(VA(0x9a98b0) + PORTRAIT, tag[2]);
    campaign_command_done(FC, leader);
    campaign_frame();
    assert(!xion); /* MU09 deliberately retains the party leader. */
    wr16(place, 0x0202);
    wr16(place + 4, 1);
    wr16(place + 6, 0);
    campaign_command_done(FC, leader);
    wr16(place + 8, 1);
    campaign_frame();
    assert(!xion);
    wr16(place + 8, 0);
    campaign_command_done(FC, leader);
    campaign_frame();
    assert(xion == guest && hud == guest && rd32(xsh) == 2 && rd32(xsh + 0x180) == 15);
    assert(campaign_progress_actor() == r && campaign_recovery_sheet(rsh) == xsh);
    assert(killed(0, x, 0, 0) == 0 && death_pending && declined);
    wr32(xsh, 0);
    angle = 3.5f;
    memcpy((void *)(guest + 0x68c), &angle, 4);
    campaign_frame();
    assert(!campaign_progress_actor() && campaign_recovery_sheet(rsh) == rsh);
    assert(ko == 1 && rd32(rsh) == 0 && rd32(rsh + 0x180) == 6 && hud == leader);
    assert(rd32(xsh) == 4 && rd32(xsh + 0x180) == 9 && notices == 2 && returned_yaw == 3.5f);
    assert(retiring == guest && release_actor(0, guest, 0, 0) == 0);
    destroy(0, guest, 0, 0);
    assert(!retiring && disposals == 1);
    simulate_field = 0;
}
static void check_castle(void) {
    static u8 attacks[8+2713*48], heights[0xa00], commands[8+60*48], objects[8+1905*96];
    wr32((u64)attacks+4,2713); wr64(VA(0x2ae57b0),(u64)attacks);
    wr32((u64)heights+4,70); wr64(VA(0x2ae57b8),(u64)heights);
    wr32((u64)commands+4,60); wr16((u64)commands+8+48*59,72); wr64(VA(0x2a161e8),(u64)commands);
    wr32((u64)objects+4,1905);
    wr32((u64)objects+8+96*89,120); wr32((u64)objects+8+96*325,442); wr64(VA(0x2a25030),(u64)objects);
    u64 vals[]={(u64)attacks,(u64)heights,(u64)commands,(u64)objects};
    for(unsigned i=0;i<sizeof edits/sizeof edits[0];i++){
        const struct TableEdit *e=edits+i;
        memcpy((void *)(vals[e->table]+e->offset),&e->original,e->width);
    }
    const int rooms[]={0x1412,0x0a12,0x1712,0x0212};
    const int selectors[]={74,57,73,10};
    for(unsigned table=0;table<4;table++){
        int sel=selectors[table];
        combat_tables(rooms[table],sel,sel,sel);
        for(unsigned i=0;i<sizeof edits/sizeof edits[0];i++){
            const struct TableEdit *e=edits+i;
            unsigned value=0;
            memcpy(&value,(void *)(vals[e->table]+e->offset),e->width);
            assert(value==(e->table==table?e->value:e->original));
        }
    }
    combat_tables(0,0,0,0);
    assert(rd32((u64)objects+0x21b4)==1003);
    wr8((u64)attacks+8+2500*48+10,4);
    combat_tables(0x1412,74,74,74);
    assert(rd8((u64)attacks+8+2500*48+10)==8);
    combat_tables(0x1412,98,98,98);
    assert(rd8((u64)attacks+8+2500*48+10)==4);
    u64 place=VA(0x717008);
    wr16(place,0x0e12);
    wr16(place+8,1);
    castle_frame();
    assert(rd16(place+4)==0 && rd16(place+6)==0 && rd16(place+8)==1);
    wr16(place+8,0);
    castle_frame();
    assert(rd16(place+4)==58 && rd16(place+6)==58 && rd16(place+8)==58);
    memset((void *)place,0,12);
}
int main(void) {
    rmc_base = calloc(1, 0x2af0000);
    assert(rmc_base);
    check_castle();
    memcpy(rmc_site(0x5b870c), original, sizeof original);
    const unsigned entries[][2] = {{0, 0x414dd0},  {8, 0x414e30},  {16, 0x414eb0},
                                   {32, 0x4150e0}, {56, 0x4151e0}, {176, 0x415380}};
    for (unsigned i = 0; i < 6; ++i)
        wr64(VA(0x5d15a0) + entries[i][0], 0x140000000ull + entries[i][1]);
    campaign_plan();
    assert(hooks == 18 && writes == 7);
    menu_and_saves();
    actors();
    enable_rows = 1;
    wr32(VA(0xabb878), 0x10002);
    wr16(VA(0x717008), 1);
    wr16(VA(0x717010), 50);
    campaign_frame();
    assert(rd32(VA(0xabb878)) == 0x10002);
    wr16(VA(0x717010), 51);
    campaign_frame();
    assert(rd32(VA(0xabb878)) == 0x10000);
    wr16(VA(0x717008), 0x1412);
    wr16(VA(0x717010), 74);
    wr8(VA(0x9ab540) + 0x24f, 0xff);
    wr32(VA(0xabb878), 3);
    campaign_frame();
    assert(rd8(VA(0x9ab540) + 0x24f) == 0xff && rd32(VA(0xabb878)) == 3);
    wr32(VA(0xabb878), 2);
    campaign_frame();
    assert(!rd8(VA(0x9ab540) + 0x24f) && !rd32(VA(0xabb878)));
    free(rmc_base);
    puts("Campaign rows, native slot copies, field policy and gauge transfers passed");
}
