#include "../registry.h"
#include "../config.h"
#include "../branch.h"
#include <assert.h>
#include <stdlib.h>
#include "../settings.h"
void cheats_plan(void), campaign_plan(void), logo_plan(void), look_plan(void);
const wchar_t *rmc_folder;
int settings_store(const char *s,const char *k,int v) {(void)s;(void)k;(void)v;abort();}
unsigned menu_buttons(void) {return 0;}
void menu_pad_text(char *s,size_t n,int row) {(void)s;(void)n;(void)row;}
int menu_key(int title) {(void)title;return 0;}
unsigned char *rmc_base;
static unsigned char *used;
static unsigned char *next;
static unsigned count, n_hooks;
void heapflight_plan(void), gameplay_plan(void), tweaks_plan(void);
void rmc_fail(const char *why) {
    fprintf(stderr, "%s\n", why);
    abort();
}
void *rmc_near(size_t n) {
    unsigned char *p = next;
    next += (n + 15) & ~(size_t)15;
    assert(next <= rmc_base + STEAM_SIZE + 65536);
    return p;
}
void rmc_write_ptr(unsigned char *p, const void *bytes, size_t n) {
    size_t at = p - rmc_base;
    assert(at < STEAM_SIZE && n <= STEAM_SIZE - at && bytes && n);
    check_text(p, n);
    for (size_t i = 0; i < n; ++i)
        assert(!used[at + i]);
    memset(used + at, 1, n);
    ++count;
}
void rmc_write(unsigned id, const void *bytes, size_t n) { rmc_write_ptr(rmc_site(id), bytes, n); }
void rmc_hook(unsigned id, void *fn, void **original) {
    size_t i = index_for(id);
    unsigned char patch[5];
    encode_detour(rmc_site(id), rmc_near(128), fn, sites[i].stolen, sites[i].fix, original, patch);
    rmc_write(id, patch, 5);
    ++n_hooks;
}
int main(int argc, char **argv) {
    assert(argc == 3);
    FILE *pe = fopen(argv[1], "rb");
    assert(pe);
    rmc_base = calloc(1, STEAM_SIZE + 65536);
    used = calloc(1, STEAM_SIZE);
    assert(rmc_base && used);
    assert(fread(rmc_base, 1, 1024, pe) == 1024);
    uint32_t nt;
    memcpy(&nt, rmc_base + 60, 4);
    uint16_t opt, nsec;
    memcpy(&opt, rmc_base + nt + 20, 2);
    memcpy(&nsec, rmc_base + nt + 6, 2);
    for (unsigned i = 0; i < nsec; ++i) {
        unsigned char *s = rmc_base + nt + 24 + opt + i * 40;
        assert(s + 40 <= rmc_base + 1024);
        uint32_t rva, n, offset;
        memcpy(&rva, s + 12, 4);
        memcpy(&n, s + 16, 4);
        memcpy(&offset, s + 20, 4);
        assert(n <= STEAM_SIZE - rva && !fseek(pe, offset, SEEK_SET) && fread(rmc_base + rva, 1, n, pe) == n);
    }
    fclose(pe);
    /* The loader has already replaced these 15 function entries when OnInit runs. */
    const unsigned panacea[] = {0x102840, 0x1063d0, 0x106440, 0x106480, 0x1064d0,
                                0x106700, 0x106790, 0x106870, 0x107240, 0x107760,
                                0x143760, 0x5373a0, 0x5373e0, 0x537420, 0x5374a0};
    for (size_t i = 0; i < COUNT(panacea); ++i)
        memset(rmc_base + panacea[i], 0xcc, 14);
    uintptr_t top = (uintptr_t)rmc_base + 0x29f33c0;
    memcpy(rmc_base + 0x749740, &top, sizeof top);
    resolve_text();
    int extra = atoi(argv[2]);
    FILE *ini = extra ? tmpfile() : fopen("../../dll/rmc.ini", "rb");
    assert(ini);
    if (extra) {
        fputs("[cheats]\ncheat_infinite_hp=on\ncheat_infinite_mp=on\ncheat_exp_mult=2\ncheat_drop_mult=10\n[retry]\nretry=on\n[savepoint]\nsavepoint_qol=on\n[worlds]\npatch_gummiskip=on\n"
              "[antiform]\nantiform_tweaks=on\ndisable_random_anti=on\ndisable_ability_anti=on\n"
              "[kh2.hacks]\nno_death=1\n[kh2.fixes]\nscd_priority=3\n",
              ini);
        rewind(ini);
    }
    char why[256];
    assert(rmc_config_load(ini, why, sizeof why));
    fclose(ini);
    next = rmc_base + STEAM_SIZE;
    heapflight_plan();
    tweaks_plan();
    gameplay_plan();
    cheats_plan();
    assert(n_hooks == (unsigned)(extra ? 50 : 43));
    settings_plan();
    look_plan();
    campaign_plan();
    logo_plan();
    printf("Combined %s plan: %u hooks, %u preflight writes, %zu loader-compatible sites\n",
           extra ? "optional" : "default", n_hooks, count, COUNT(sites));
    free(used);
    free(rmc_base);
}
