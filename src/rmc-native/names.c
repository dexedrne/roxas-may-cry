#include "settings.h"
#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

int look_current(void);
static const unsigned char roxas[] = {0x3f, 0xa8, 0xb1, 0x9a, 0xac, 0};
static const unsigned char xion[] = {0x45, 0xa2, 0xa8, 0xa7};
static const unsigned char operands[32] = {0, 0, 0, 0, 1, 6, 1, 4, 3, 1, 1, 1, 1, 0, 1, 5,
                                           0, 4, 2, 4, 2, 2, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1};
struct name {
    struct name *next;
    size_t size;
    int id;
    unsigned char data[]; /* source followed by the widget's stable display buffer */
};
static struct name *names;
static atomic_flag guard = ATOMIC_FLAG_INIT;
static void lock(void) {
    while (atomic_flag_test_and_set_explicit(&guard, memory_order_acquire)) {
    }
}
static void unlock(void) { atomic_flag_clear_explicit(&guard, memory_order_release); }
static int player_reference(int id, const unsigned char *src, size_t at, int all) {
    if (all)
        return 1;
    switch (id) {
    case 0x51f:
    case 0x52d:
    case 0x534a:
    case 0x2ce8:
    case 0x2d0c:
    case 0x2e9a:
        return !at && !src[5];
    case 0x453a:
    case 0x50e5:
    case 0x511d:
    case 0x533f:
        return at == (id == 0x453a ? 5u : 0u) && (src[at + 5] == 0xec || src[at + 5] == 0xee);
    default:
        return 0;
    }
}
static void render(struct name *n, int selected, int all) {
    const unsigned char *src = n->data;
    unsigned char *dst = n->data + n->size;
    for (size_t i = 0; src[i];) {
        if (selected && !strncmp((const char *)src + i, (const char *)roxas, 5) &&
            player_reference(n->id, src, i, all)) {
            memcpy(dst, xion, 4);
            dst += 4;
            i += 5;
        } else {
            size_t step = 1 + (src[i] < 32 ? operands[src[i]] : 0);
            memcpy(dst, src + i, step);
            dst += step;
            i += step;
        }
    }
    *dst = 0;
}
static int selected, all_names;
void names_refresh(void) {
    int next = !!look_current(), all = rmc_int("gameplay", "names_in_text");
    lock();
    if (next != selected || all != all_names) {
        selected = next;
        all_names = all;
        for (struct name *n = names; n; n = n->next)
            render(n, selected, all_names);
    }
    unlock();
}
const unsigned char *names_display(int id, const unsigned char *src) {
    id &= 0x7fff;
    if (!src || id == 0x377a || id == 0x48bc || id == 0x491a || id == 0x4ed9 || id == 0x4f43)
        return src;
    size_t size = 0;
    int match = 0;
    while (size < 65536 && src[size]) {
        unsigned c = src[size];
        if (c == roxas[0] && !strncmp((const char *)src + size, (const char *)roxas, 5))
            match = 1;
        size += 1 + (c < 32 ? operands[c] : 0);
    }
    if (!match || size >= 65536)
        return src;
    ++size;
    lock();
    struct name *n = names;
    while (n && (n->id != id || n->size != size || memcmp(n->data, src, size)))
        n = n->next;
    if (!n) {
        n = malloc(sizeof *n + 2 * size);
        if (n) {
            n->next = names;
            n->size = size;
            n->id = id;
            memcpy(n->data, src, size);
            render(n, selected, all_names);
            names = n;
        }
    }
    unlock();
    return n ? n->data + size : src;
}
