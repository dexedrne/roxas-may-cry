#pragma once
#include <stddef.h>
void look_plan(void);
void look_frame(void);
const char *look_request(int target,int save);
const char *look_reason(int save);
void look_reset(void);
int look_current(void);
int look_available(int target);
void look_save_open(void);
void look_shopface(char *path,size_t cap);
