#pragma once
#include <stdint.h>

void campaign_plan(void);
int campaign_mode(void);
const unsigned char *campaign_message(int id);

void campaign_command_done(uint64_t controller, uint64_t actor);
uint64_t campaign_progress_actor(void);
uint64_t campaign_recovery_sheet(uint64_t sheet);
void campaign_frame(void);
void campaign_handback(void);
