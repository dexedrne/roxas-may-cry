#pragma once
#include <stdint.h>
extern uint64_t (*rmc_stock_damage)(uint64_t,uint64_t,uint64_t,uint64_t);
void rmc_debug_damage(int enemy, int active);
void difficulty_plan(void);
void rmc_add_exp(uint64_t *args);
