#pragma once
#include <stddef.h>
#include <stdint.h>
extern const wchar_t *rmc_folder;
int settings_store(const char *section, const char *key, int value);
void settings_plan(void);
void cheats_frame(void);
void cheats_refresh(void);
void menu_encode(unsigned char *dst, const char *src);
const unsigned char *names_display(int id, const unsigned char *src);
void names_refresh(void);
