/* Host-only plan recorder. All input is data; no function from the PE is called. */
#include "heapflight.c"
#include <assert.h>
#include <stdlib.h>

unsigned char *rmc_base;
static int option_mode;
static unsigned hooks;
static int last_arg[2];

int rmc_int(const char *section, const char *key) {
    if (option_mode == 1) return 0;
    if (!strcmp(key,"size")) return !strcmp(section,"kh2.cache") ? 524288 : 131072;
    if (!strcmp(key,"player") || !strcmp(key,"friend")) return 32768;
    if (!strcmp(key,"scd_priority")) return option_mode == 2 ? 3 : 1;
    if (!strcmp(key,"fast_title")) return 1;
    if (!strcmp(key,"no_death")) return option_mode == 2;
    return 0;
}
void rmc_fail(const char *message) {
    fprintf(stderr, "heap/flight test failed: %s\n", message);
    abort();
}
unsigned char *rmc_site(unsigned id) { return rmc_base + id; }
void rmc_write_ptr(unsigned char *p, const void *bytes, size_t len) { memcpy(p, bytes, len); }
void rmc_write(unsigned id, const void *bytes, size_t len) { rmc_write_ptr(rmc_site(id), bytes, len); }
static void record(int a, int b) { last_arg[0] = a; last_arg[1] = b; }
void rmc_hook(unsigned id, void *handler, void **original) {
    assert((id == 0x39e260 && handler == (void *)set_block) ||
           (id == 0x39e270 && handler == (void *)set_party));
    *original = (void *)record;
    ++hooks;
}

void check_plan(unsigned char *image, int mode) {
    rmc_base = image;
    option_mode = mode;
    hooks = 0;
    heapflight_plan();
    assert(hooks == (mode == 1 ? 0u : 2u));
    if (hooks) {
        set_block(2, INT32_MAX - 1); assert(last_arg[0] == 2 && last_arg[1] == INT32_MAX);
        set_block(3, 1); assert(last_arg[1] == (32 << 20) + 1);
        set_block(4, 2); assert(last_arg[1] == (32 << 20) + 2);
        set_block(6, -1); assert(last_arg[1] == -1);
        set_party(0, INT32_MAX); assert(last_arg[0] == (32 << 20) && last_arg[1] == INT32_MAX);
    }
    assert(row_step(0) == 28 && row_step(1) == 22 && row_step(2) == 28);
    for (int i = 0; i < 4; ++i) {
        uintptr_t ip = (uintptr_t)effect_stores[i];
        assert(skip_length(ip, 0, 1) == (i == 0 ? 3 : 9));
        assert(!skip_length(ip, 1, 1) && !skip_length(ip, 0, 0) && !skip_length(ip+1, 0, 1));
    }
}
