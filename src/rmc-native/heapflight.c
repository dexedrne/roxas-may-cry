#include "heapflight.h"
#include "rmc.h"
#include "config.h"
#include <stdint.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

/* Site IDs index the signature registry. RIP-relative operands and inter-site
 * displacements come from the running image, including the menu setter VA. */
static void relative_operand(unsigned char *field, const unsigned char *next,
                             const unsigned char *target) {
    intptr_t distance = (intptr_t)target - (intptr_t)next;
    if (distance < INT32_MIN || distance > INT32_MAX) {
        rmc_fail("flight branch displacement exceeds 32 bits");
        return;
    }
    int32_t rel = (int32_t)distance;
    memcpy(field, &rel, sizeof rel);
}

static unsigned char *rip_target(unsigned char *instruction, unsigned len) {
    int32_t offset;
    memcpy(&offset, instruction + len - 4, sizeof offset);
    return instruction + len + offset;
}

static unsigned char *row_operand;
static unsigned char (*compact_menu)(void);
static unsigned char row_step(unsigned char compact) {
    return compact == 1 ? 22 : 28;
}

static void plan_flight(void) {
    unsigned char *entry = rmc_site(0x424573);
    unsigned char *resume = rmc_site(0x424578);
    unsigned char *cave = rmc_site(0x4245f2);
    unsigned char branch[] = {0xe9,0,0,0,0};
    unsigned char hover[] = {0x0f,0x57,0xdb,0x33,0xd2,0x83,0xc2,20,0xe9,0,0,0,0};
    relative_operand(branch + 1, entry + sizeof branch, cave);
    relative_operand(hover + 9, cave + sizeof hover, resume);
    rmc_write(0x424573, branch, sizeof branch);
    rmc_write(0x4245f2, hover, sizeof hover);
    rmc_write(0x4245a7, (unsigned char[]){22}, 1);
    rmc_write(0x4245ca, (unsigned char[]){0x33,0xd2,0x83,0xc2,21}, 5);

    unsigned char menu[] = {
        0xf3,0x0f,0x10,0x15,0,0,0,0, /* movss xmm2,[y] */
        0x8b,0xd7,0x49,0x8b,0xce,     /* member and menu object */
        0x48,0xb8,0,0,0,0,0,0,0,0,   /* mov rax,set_y */
        0xff,0xd0,0x90
    };
    uintptr_t setter = (uintptr_t)rmc_site(0x2ffa30);
    float y_offset = -25.0f;
    relative_operand(menu + 4, rmc_site(0x30661b) + 8, rmc_site(0x306784));
    memcpy(menu + 15, &setter, sizeof setter);
    rmc_write(0x30661b, menu, sizeof menu);
    rmc_write(0x306784, &y_offset, sizeof y_offset);
    compact_menu = (unsigned char (*)(void))rmc_site(0x2e5380);
    row_operand = rmc_site(0x15b962);
}

static void *reservations[2];
static int block_extra[7];
static void (*set_block_original)(int, int);
static void (*set_party_original)(int, int);

static int increased(int bytes, int extra) {
    return bytes > INT32_MAX - extra ? INT32_MAX : bytes + extra;
}
static void set_block(int index, int bytes) {
    int extra = (unsigned)index < 7 ? block_extra[index] : 0;
    set_block_original(index, increased(bytes, extra));
}
static void set_party(int player, int friend_size) {
    set_party_original(increased(player, block_extra[2]), increased(friend_size, block_extra[3]));
}

static unsigned char *allocate_pool(int index, size_t size) {
    const size_t alignment = 32u << 20;
#ifdef _WIN32
    void *raw = VirtualAlloc(NULL, size + alignment - 1, MEM_RESERVE, PAGE_NOACCESS);
    if (!raw) return NULL;
    reservations[index] = raw;
    uintptr_t aligned = ((uintptr_t)raw + alignment - 1) & ~(uintptr_t)(alignment - 1);
    return VirtualAlloc((void *)aligned, size, MEM_COMMIT, PAGE_READWRITE);
#else
    /* The offline write collector records symbolic pools and never accesses them. */
    (void)size;
    return reservations[index] = (void *)(uintptr_t)((80u + index * 16u) * (uint64_t)alignment);
#endif
}

static void plan_pool(int data, int kib) {
    if (!kib) return;
    if (kib < 0) { rmc_fail("negative pool size"); return; }
    unsigned char *a = rmc_site(data ? 0x3e5f90 : 0x39d719);
    unsigned char *b = rmc_site(data ? 0x3e5f3d : 0x39dac1);
    unsigned char *pool = rip_target(a, 7);
    unsigned char *other = rip_target(rmc_site(data ? 0x3e5f36 : 0x39daba), 7);
    uintptr_t initial, first;
    memcpy(&initial, other, sizeof initial);
    memcpy(&first, pool, sizeof first);
    uintptr_t expected = data ? 0 : (uintptr_t)pool + (30u << 20);
    if (pool != rip_target(b, 7) || first || initial != expected) {
        rmc_fail("pool operands disagree or the game already uses the pool");
        return;
    }
    if (!data) {
        uintptr_t head;
        memcpy(&head, pool + (30u << 20) + 8, sizeof head);
        if (head) { rmc_fail("resource cache is already populated"); return; }
    }
    size_t size = (size_t)kib << 10;
    unsigned char *moved = allocate_pool(data, size);
    if (!moved) { rmc_fail("unable to allocate aligned game pool"); return; }
    rmc_write_ptr(pool, &moved, sizeof moved);
    if (!data) {
        uintptr_t top = (uintptr_t)moved + size;
        rmc_write_ptr(other, &top, sizeof top);
    }
    const unsigned char mov = 0x8b;
    rmc_write_ptr(a + 1, &mov, 1);
    rmc_write_ptr(b + 1, &mov, 1);
}

static void plan_blocks(void) {
    static const char *const keys[] = {"magic", "player", "friend", "friend", "drive", "menu"};
    int enabled = 0;
    for (unsigned i = 0; i < 6; ++i) {
        int kib = rmc_int("kh2.cache", keys[i]);
        if ((unsigned)kib > 0x1fffff) { rmc_fail("invalid reserve block size"); return; }
        block_extra[i + 1] = kib * 1024;
        enabled |= kib;
    }
    if (enabled) {
        rmc_hook(0x39e260, (void *)set_block, (void **)&set_block_original);
        rmc_hook(0x39e270, (void *)set_party, (void **)&set_party_original);
    }
}

static unsigned char *effect_stores[4];
static int skip_length(uintptr_t ip, uintptr_t object, int is_write) {
    if (!object && is_write) {
        for (unsigned i = 0; i < 4; ++i)
            if (ip == (uintptr_t)effect_stores[i]) return i == 0 ? 3 : 9;
    }
    return 0;
}
#ifdef _WIN32
static void *exception_handler;
static HANDLE row_timer;
static LONG row_active;

static void update_row(void) {
    unsigned char desired = row_step(compact_menu());
    if (*row_operand == desired) return;
    DWORD previous, ignored;
    if (!VirtualProtect(row_operand, 1, PAGE_EXECUTE_READWRITE, &previous))
        rmc_fail("cannot change command row protection");
    *(volatile unsigned char *)row_operand = desired;
    if (!FlushInstructionCache(GetCurrentProcess(), row_operand, 1) ||
        !VirtualProtect(row_operand, 1, previous, &ignored))
        rmc_fail("cannot publish command row change");
}
static VOID CALLBACK poll_row(PVOID context, BOOLEAN timed_out) {
    (void)context;
    (void)timed_out;
    if (InterlockedCompareExchange(&row_active, 0, 0)) update_row();
}

static LONG CALLBACK effect_exception(EXCEPTION_POINTERS *exception) {
    EXCEPTION_RECORD *record = exception->ExceptionRecord;
    CONTEXT *ctx = exception->ContextRecord;
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
        record->ExceptionFlags & EXCEPTION_NONCONTINUABLE || record->NumberParameters < 2)
        return EXCEPTION_CONTINUE_SEARCH;
    int length = skip_length(ctx->Rip, ctx->Rbx, record->ExceptionInformation[0] == 1);
    if (!length) return EXCEPTION_CONTINUE_SEARCH;
    ctx->Rip += length;
    return EXCEPTION_CONTINUE_EXECUTION;
}
#endif

void heapflight_plan(void) {
    if (rmc_int("kh2.hacks", "test_crash")) {
        rmc_fail("test_crash requested an intentional startup failure");
        return;
    }
    plan_pool(0, rmc_int("kh2.cache", "size"));
    plan_pool(1, rmc_int("kh2.data", "size"));
    plan_blocks();
    int priority = rmc_int("kh2.fixes", "scd_priority");
    if (priority & 1)
        rmc_write(0x927e2, (unsigned char[]){0x31,0xc0,0x0f,0x1f,0}, 5);
    if (priority & 2)
        rmc_write(0x92758, (unsigned char[]){0xa9,0,0,0,0}, 5);
    if (rmc_int("kh2.hacks", "fast_title"))
        rmc_write(0x2b46eb, (unsigned char[]){0xbf,0x11,0,0,0}, 5);
    if (rmc_int("kh2.hacks", "no_death"))
        rmc_write(0x404d60, (unsigned char[]){0xc3,0x90}, 2);
    static const unsigned stores[] = {0x4d3a84,0x4d3aec,0x4d3afb,0x4d3b0a};
    for (unsigned i = 0; i < 4; ++i) effect_stores[i] = rmc_site(stores[i]);
    plan_flight();
}

int heapflight_ready(void) {
#ifdef _WIN32
    if (rmc_int("kh2.hacks", "attempt_continue_after_exception_but_probably_make_things_worse")) {
        exception_handler = AddVectoredExceptionHandler(1, effect_exception);
        if (!exception_handler) { rmc_fail("unable to install effect-pool continuation"); return 0; }
    }
    /* The loader holds its lock here. No fallible attach step may wait on a live timer. */
    if (!CreateTimerQueueTimer(&row_timer, NULL, poll_row, NULL, 250, 250, WT_EXECUTEINTIMERTHREAD)) {
        rmc_fail("unable to schedule command row update");
        return 0;
    }
#endif
    return 1;
}

void heapflight_cleanup(void) {
#ifdef _WIN32
    /* An inactive callback only reads row_active. Never wait inside loader initialization. */
    if (row_timer) DeleteTimerQueueTimer(NULL, row_timer, NULL);
    row_timer = NULL;
    if (exception_handler) RemoveVectoredExceptionHandler(exception_handler);
    exception_handler = NULL;
    for (unsigned i = 0; i < 2; ++i) {
        if (reservations[i]) VirtualFree(reservations[i], 0, MEM_RELEASE);
        reservations[i] = NULL;
    }
#endif
}

void heapflight_start(void) {
#ifdef _WIN32
    fputs("flytest\n", stdout);
    fflush(stdout);
    update_row();
    InterlockedExchange(&row_active, 1);
#endif
}
