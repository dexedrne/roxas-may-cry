#pragma once
#include "rmc.h"
#include "config.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef int64_t s64;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;
typedef u64 (*Fn4)(u64, u64, u64, u64);
typedef u64 (*Fn5)(u64, u64, u64, u64, u64);
#define call4(id, a, b, c, d) ((Fn4)rmc_site(id))((u64)(a), (u64)(b), (u64)(c), (u64)(d))
#define call5(id, a, b, c, d, e) ((Fn5)rmc_site(id))((u64)(a), (u64)(b), (u64)(c), (u64)(d), (u64)(e))
#define DEFINE_ACCESS(width)                                                                                 \
    static inline u##width rd##width(uintptr_t address) {                                                    \
        u##width v;                                                                                          \
        memcpy(&v, (const void *)address, sizeof v);                                                         \
        return v;                                                                                            \
    }                                                                                                        \
    static inline void wr##width(uintptr_t address, u##width v) { memcpy((void *)address, &v, sizeof v); }
DEFINE_ACCESS(8)
DEFINE_ACCESS(16)
DEFINE_ACCESS(32)
DEFINE_ACCESS(64)
#undef DEFINE_ACCESS
static inline uintptr_t data_at(unsigned id) { return (uintptr_t)rmc_site(id); }
