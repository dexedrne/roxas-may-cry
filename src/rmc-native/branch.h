#pragma once
#include "rmc.h"
#include <string.h>
static int32_t rel32(uintptr_t to, uintptr_t end) {
    intptr_t d = (intptr_t)(to - end);
    if (d < INT32_MIN || d > INT32_MAX)
        rmc_fail("Branch outside signed displacement range");
    return (int32_t)d;
}
static void jump(unsigned char *p, const void *at, const void *to) {
    int32_t d = rel32((uintptr_t)to, (uintptr_t)at + 5);
    p[0] = 0xe9;
    memcpy(p + 1, &d, 4);
}
static void encode_detour(unsigned char *target, unsigned char *relay, void *replacement, unsigned span,
                          unsigned fix, void **original, unsigned char patch[5]) {
    memcpy(relay, "\xff\x25\0\0\0\0", 6);
    memcpy(relay + 6, &replacement, 8);
    if (original) {
        if (span < 5 || span > 32)
            rmc_fail("Missing trampoline instruction boundaries");
        unsigned char *tramp = relay + 16;
        memcpy(tramp, target, span);
        if (fix) {
            size_t off = fix - 1;
            int32_t d;
            memcpy(&d, target + off, 4);
            d = rel32((uintptr_t)target + d, (uintptr_t)tramp);
            memcpy(tramp + off, &d, 4);
        }
        jump(tramp + span, tramp + span, target + span);
        *original = tramp;
    }
    jump(patch, target, relay);
}
