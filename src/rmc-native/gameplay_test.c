#include "gameplay.c"
#include <assert.h>
#include <setjmp.h>

static uint64_t script_table;
static uintptr_t command_table;
static unsigned char commands[8 + 48 * 2];
static word handle_result, last_handle;
static word reset_args[4];
static jmp_buf rejected;
static int expecting_failure, detours, exp_calls;
void difficulty_plan(void) {}
void rmc_add_exp(uint64_t *slots) {
    assert(slots[0] == 1 && slots[1] == 2);
    ++exp_calls;
}
static _Alignas(word) unsigned char player[0xdd8], actor[0x5c8], sheet[0x258];
static _Alignas(word) unsigned char party[0xf4], form[0x38], reaction[0xb43];
static word player_ptr, system_table;
static uint32_t camera_mode;
static uint64_t pad[2];
static unsigned char growth;
static word engine_return, engine_args[4];
static unsigned engine_id;
static uint16_t weapon_info[] = {0, 12};

static word player_get(word a, word b, word c, word d) { return player_ptr; }
static word pad_get(word a, word b, word c, word d) { return (word)pad; }
static word item_get(word a, word b, word c, word d) { return a == 7 ? (word)weapon_info : 0; }
static word info_get(word a, word b, word c, word d) { assert(a == (word)weapon_info); return a; }
static word resolve_handle(word id, word b, word c, word d) {
    last_handle = id;
    switch (id) {
    case 10: return (word)actor;
    case 11: return (word)party;
    case 12: return (word)form;
    case 0: return 0;
    default: return handle_result;
    }
}
static word record(word a, word b, word c, word d) {
    engine_args[0] = a;
    engine_args[1] = b;
    engine_args[2] = c;
    engine_args[3] = d;
    return engine_return;
}
static word map_jump(void *rec, uint32_t effect, uint32_t fade, int zero, uint32_t flag) {
    const uint8_t expected[] = {1, 2, 3, 0, 0x23, 1, 0x45, 2, 0x67, 3};
    assert(!memcmp(rec, expected, sizeof expected));
    assert(effect == 7 && fade == 8 && zero == 0 && flag == 9);
    return 99;
}
static word handle(word p, word b, word c, word d) {
    (void)b;
    (void)c;
    (void)d;
    last_handle = p;
    return handle_result;
}
static word tail(word a, word b, word c, word d) {
    reset_args[0] = a;
    reset_args[1] = b;
    reset_args[2] = c;
    reset_args[3] = d;
    return 99;
}
unsigned char *rmc_site(unsigned id) {
    switch (id) {
    case 0x7534a8:
        return (void *)&script_table;
    case 0x2a161e8:
        return (void *)&command_table;
    case 0x4ad240:
        return (void *)handle;
    case 0x4ad270: return (void *)resolve_handle;
    case 0x3b3e80: return (void *)player_get;
    case 0x39b760: return (void *)pad_get;
    case 0x3e39a0: return (void *)item_get;
    case 0x3e3ef0: return (void *)info_get;
    case 0x5c9030: return &growth;
    case 0x2ae5768: return (void *)&system_table;
    case 0x718ca8: return (void *)&camera_mode;
    case 0x152990: return (void *)map_jump;
    case 0x3c0cd0: case 0x3eafc0: case 0x3c2100:
    case 0x3c4720: case 0x3e4530: case 0x3cb9f0:
        engine_id = id;
        return (void *)record;
    case 0x3e1620:
        return (void *)tail;
    default:
        assert(0);
        return NULL;
    }
}
void rmc_hook(unsigned id, void *fn, void **original) {
    assert(fn && !original && (id == 0x1f9a80 || id == 0x3a9cf0));
    ++detours;
}
void rmc_fail(const char *why) {
    assert(expecting_failure && why && *why);
    longjmp(rejected, 1);
}
void rmc_write(unsigned id, const void *p, size_t n) {
    assert(id == 0x7534a8 && n == sizeof script_table);
    memcpy(&script_table, p, n);
}
static void check_ability_count(void) {
    uint64_t args[] = {UINT64_C(0xdeadbeef0000000a), 12};
    FIELD(word, actor, 0x5c0) = (word)sheet;
    descriptors[46].invoke(args);
    assert(args[0] == UINT64_C(0xdeadbeef0000000a)); /* No party sheet. */
    FIELD(uint32_t, sheet, 0x250) = 11;
    FIELD(uint32_t, sheet, 0x254) = 12;
    FIELD(uint16_t, party, 0) = 7;
    party[0x10] = 2;
    FIELD(uint16_t, party, 0x14) = 7;
    FIELD(uint16_t, party, 0x16) = 8; /* Missing optional item. */
    FIELD(uint16_t, party, 0x54) = 0x800c;
    FIELD(uint16_t, party, 0x56) = 12;
    FIELD(uint16_t, form, 0) = 9; /* Rechecks the party weapon. */
    FIELD(uint16_t, form, 8 + 2 * 23) = 0x800c;
    descriptors[46].invoke(args);
    assert(args[0] == UINT64_C(0xdeadbeef00000005));
}

static void check_shipped_handlers(void) {
    uint64_t jump[] = {257, 258, 259, 0x10123, 0x10245, 0x10367, 7, 8, UINT64_C(0x100000009)};
    assert(descriptors[8].invoke(jump) == 99);
    uint64_t args[] = {10, 19};
    descriptors[13].invoke(args);
    assert(engine_id == 0x3c0cd0 && engine_args[0] == (word)actor && engine_args[1] == 19);
    engine_return = (word)sheet;
    handle_result = 72;
    descriptors[15].invoke(args);
    assert(engine_id == 0x3eafc0 && engine_args[0] == 10 && engine_args[1] == 19);
    assert(last_handle == (word)sheet && args[0] == 72);
    args[0] = 10;
    descriptors[33].invoke(args);
    assert(engine_id == 0x3c2100 && engine_args[0] == (word)actor);
    pad[1] = UINT64_C(1) << 51; /* Sign extension of the bit-31 mask. */
    args[0] = 63;
    descriptors[34].invoke(args);
    assert(args[0] == 1);
    args[0] = 63;
    descriptors[35].invoke(args);
    assert(args[0] == 0);
    check_ability_count();
    descriptors[47].invoke(args);
    assert(args[0] == 72 && last_handle == (word)&growth);
    player_ptr = (word)player;
    FIELD(word, player, 0xdd0) = (word)reaction;
    reaction[0xb42] = 0xfe;
    descriptors[48].invoke(args);
    assert(args[0] == 0xfe);
    args[0] = 0x10b;
    camera_mode = 0x12345600;
    system_table = 1;
    descriptors[55].invoke(args);
    assert(camera_mode == 0x1234560b);
    engine_return = UINT64_C(0xf00000002);
    descriptors[78].invoke(args);
    assert(engine_id == 0x3c4720 && engine_args[0] == 0x10b && args[0] == 2);
    args[0] = 7;
    descriptors[80].invoke(args);
    assert(engine_id == 0x3e4530 && engine_args[0] == 7);
    args[0] = 0x3fc00000;
    descriptors[87].invoke(args);
    assert(drive_scale == 1.5f);
    descriptors[93].invoke(args);
    assert(engine_id == 0x3cb9f0 && !engine_args[0] && engine_args[1] == player_ptr && engine_args[2] == 1);
}
static void check_drive_writes(void) {
    uint64_t args[] = {42, 0x104};
    player_ptr = (word)player;
    FIELD(uint32_t, player, 4) = 10;
    FIELD(word, actor, 0x5c0) = (word)sheet;
    command_table = 0;
    memset(sheet + 0x1b0, 0xa5, 4);
    for (int i = 0; i < 2; ++i) {
        handle_result = (word)sheet + 0x1b1 + i;
        descriptors[69].invoke(args);
    }
    assert(!memcmp(sheet + 0x1b0, "\xa5\x04\x04\xa5", 4));
    handle_result = (word)sheet + 0x1b3;
    expecting_failure = 1;
    if (!setjmp(rejected)) {
        descriptors[69].invoke(args);
        assert(0);
    }
    assert(sheet[0x1b3] == 0xa5);
    expecting_failure = 0;
}
int main(void) {
    char err[128];
    assert(rmc_config_load(NULL, err, sizeof err));
    gameplay_plan();
    assert(detours == 2 && script_table == (uintptr_t)descriptors);
    for (unsigned i = 0; i < 114; ++i)
        assert(descriptors[i].invoke);
    assert(descriptors[3].flags == 0x40000002 && descriptors[69].flags == 2);
    uint64_t args[] = {UINT64_C(0x01020304abcd0000), 0x80000019};
    handle_result = 0xABA;
    descriptors[3].invoke(args);
    assert(last_handle == UINT64_C(0xffffffff80000019) && args[0] == UINT64_C(0x0102030400000aba));
    command_table = (uintptr_t)commands;
    FIELD(int32_t, commands, 4) = 2;
    handle_result = command_table + 8 + 48 + 47;
    args[0] = 42;
    args[1] = 2;
    descriptors[69].invoke(args);
    assert(commands[sizeof commands - 1] == 2 && last_handle == 42);
    --handle_result;
    expecting_failure = 1;
    if (!setjmp(rejected)) {
        descriptors[69].invoke(args);
        assert(0);
    }
    if (!setjmp(rejected)) {
        descriptors[113].invoke(args);
        assert(0);
    }
    expecting_failure = 0;
    args[0] = 1;
    descriptors[1].invoke(args);
    assert(exp_calls == 1);
    script_table = 0;
    assert(reset_table(1, 2, 3, 4) == 99 && script_table == (uintptr_t)descriptors);
    assert(reset_args[0] == 1 && reset_args[3] == 4);
    check_shipped_handlers();
    check_drive_writes();
    puts("All 16 shipped script indices, tt28 drive writes, slot width, failure paths and reset passed");
}

uint64_t campaign_progress_actor(void) { return 0; }
