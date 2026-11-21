#pragma once
struct site {
    unsigned id;
    const char *signature;
    unsigned short length;
    int offset;
    unsigned operand, next;
    int delta;
    unsigned stolen, fix;
    const char *unwind;
    unsigned unwind_len;
};
