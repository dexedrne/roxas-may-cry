#include "../branch.h"
#include "../site.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static const struct site sites[] = {
#include "sites.inc"
};
void rmc_fail(const char *why) {
    fprintf(stderr, "%s\n", why);
    abort();
}
static unsigned char *destination(unsigned char *p) {
    int32_t offset;
    memcpy(&offset, p + 1, 4);
    assert(*p == 0xe9);
    return p + 5 + offset;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *pe = fopen(argv[1], "rb");
    assert(pe);
    unsigned char *arena = malloc(TEXT_SIZE + 256);
    assert(arena);
    unsigned char *text = arena + 128;
    /* The PE identity and section file layout are pinned by the registry generator. */
    assert(!fseek(pe, 0x400, SEEK_SET) && fread(text, 1, TEXT_SIZE, pe) == TEXT_SIZE);
    fclose(pe);
    size_t n = 0, nfix = 0;
    for (size_t i = 0; i < sizeof sites / sizeof *sites; ++i) {
        const struct site *s = sites + i;
        if (!s->stolen)
            continue;
        if (s->unwind_len) {
            assert(s->unwind[0] == 1 && (unsigned char)s->unwind[1] <= s->stolen);
            assert(s->unwind_len == 4u + 2u * (unsigned char)s->unwind[2]);
        }
        if (s->id == 0x3a0660)
            assert(s->unwind_len == 6 && (unsigned char)s->unwind[5] == 0x42);
        ++n;
        nfix += (s->fix != 0);
        unsigned char *site = text + s->id - TEXT_START;
        for (unsigned side = 0; side < 2; ++side) {
            unsigned char *relay = side ? text + TEXT_SIZE : arena;
            unsigned char patch[5], expected[32];
            void *orig = NULL, *handler = expected;
            encode_detour(site, relay, handler, s->stolen, s->fix, &orig, patch);
            int32_t branch;
            memcpy(&branch, patch + 1, 4);
            assert(patch[0] == 0xe9 && site + 5 + branch == relay);
            assert(orig == relay + 16 && destination(relay + 16 + s->stolen) == site + s->stolen);
            assert(!memcmp(relay, "\xff\x25\0\0\0\0", 6) && !memcmp(relay + 6, &handler, 8));
            memcpy(expected, site, s->stolen);
            if (s->fix) {
                int32_t before, after;
                unsigned off = s->fix - 1;
                memcpy(&before, site + off, 4);
                memcpy(&after, relay + 16 + off, 4);
                assert((intptr_t)site + before == (intptr_t)orig + after);
                memcpy(expected + off, &after, 4);
            }
            assert(!memcmp(orig, expected, s->stolen));
        }
    }
    free(arena);
    printf("%zu trampoline prologues checked with relays above and below, %zu relative operands\n", n, nfix);
}
