#pragma once
#include <stddef.h>
#include <stdint.h>
extern unsigned char *rmc_base;
unsigned char *rmc_site(unsigned id);
void rmc_fail(const char *message);
void *rmc_near(size_t size);
void rmc_write(unsigned id, const void *bytes, size_t length);
void rmc_write_ptr(unsigned char *address, const void *bytes, size_t length);
void rmc_hook(unsigned id, void *replacement, void **original);
