/* Compiled into the plugin and the offline combined-plan check. */
#pragma once
#include "rmc.h"
#include <string.h>
#include <stdio.h>
#include "site.h"
static struct site const sites[] = {
#include "sites.inc"
};
#define COUNT(x) (sizeof(x) / sizeof(*(x)))
static unsigned char *resolved[COUNT(sites)], *anchors[COUNT(sites)];
static size_t index_for(unsigned id) {
    size_t lo = 0, hi = COUNT(sites);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (sites[mid].id < id)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == COUNT(sites) || sites[lo].id != id)
        rmc_fail("Unregistered game site");
    return lo;
}
unsigned char *rmc_site(unsigned id) { return resolved[index_for(id)]; }

static void resolve_text(void) {
    unsigned char *start = rmc_base + TEXT_START;
    for (size_t i = 0; i < COUNT(sites); ++i) {
        const struct site *s = &sites[i];
        unsigned char *p = start, *last = start + TEXT_SIZE - s->length, *hit = NULL;
        while (p <= last && (p = memchr(p, (unsigned char)s->signature[0], (size_t)(last - p) + 1))) {
            if (!memcmp(p, s->signature, s->length)) {
                if (hit) {
                    hit = NULL;
                    break;
                }
                hit = p;
            }
            ++p;
        }
        if (!hit) {
            char why[100];
            snprintf(why, sizeof why, "Missing or ambiguous Steam signature %x", s->id);
            rmc_fail(why);
        }
        anchors[i] = hit;
        hit += s->offset;
        if (s->next) {
            int32_t disp;
            memcpy(&disp, hit + s->operand, 4);
            hit += (intptr_t)s->next + disp + s->delta;
        }
        if (hit != rmc_base + s->id)
            rmc_fail("Game site does not match pinned Steam layout");
        resolved[i] = hit;
    }
}

static void check_text(unsigned char *p, size_t n) {
    size_t offset = (size_t)(p - rmc_base);
    if (offset >= TEXT_START && offset < TEXT_START + TEXT_SIZE) {
        size_t i = 0;
        for (; i < COUNT(sites); ++i) {
            size_t skip = (uintptr_t)p - (uintptr_t)anchors[i];
            if (skip <= sites[i].length && n <= sites[i].length - skip &&
                !memcmp(p, sites[i].signature + skip, n))
                break;
        }
        if (i == COUNT(sites))
            rmc_fail("Missing or modified instruction bytes at game edit");
    }
}
