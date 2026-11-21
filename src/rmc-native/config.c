#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdatomic.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH = 19 };
enum type { BOOL, WHOLE, REAL, MULTIPLIER, VISIBILITY };
struct field {
    const char *section, *key;
    enum type type;
    unsigned count;
    double low, high, defaults[WIDTH];
};
static const struct field fields[] = {
#include "config_fields.inc"
};
enum { COUNT = sizeof fields / sizeof fields[0] };
static _Atomic double values[COUNT][WIDTH];

static int equal(const char *a, const char *b)
{
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++;
        b++;
    }
    return !*a && !*b;
}

static char *trim(char *text)
{
    while (isspace((unsigned char)*text)) text++;
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) *--end = 0;
    return text;
}

static int lookup(const char *section, const char *key)
{
    for (int i = 0; i < COUNT; i++)
        if (equal(section, fields[i].section) && equal(key, fields[i].key)) return i;
    return -1;
}

double rmc_value(const char *section, const char *key, unsigned index)
{
    int i = lookup(section, key);
    if (i < 0 || index >= fields[i].count) {
        fprintf(stderr, "rmc: invalid configuration lookup [%s] %s[%u]\n", section, key, index);
        abort();
    }
    return values[i][index];
}

int rmc_int(const char *section, const char *key)
{
    return (int)rmc_value(section, key, 0);
}

static int parse(const struct field *field, const char *text, double *out)
{
    if (field->type == VISIBILITY) {
        if (equal(text,"hide")) *out=1;
        else if (equal(text,"show")) *out=0;
        else return 0;
        return 1;
    }
    if (field->type == BOOL) {
        if (equal(text, "on") || equal(text, "true") || equal(text, "1")) *out = 1;
        else if (equal(text, "off") || equal(text, "false") || equal(text, "0")) *out = 0;
        else return 0;
        return 1;
    }
    for (unsigned j = 0; j < field->count; j++) {
        char *end;
        errno = 0;
        double value = strtod(text, &end);
        double high = j && !strncmp(field->key, "symbol_", 7) ? 255 : field->high;
        if (end == text || errno || !isfinite(value) || value < field->low || value > high)
            return 0;
        if (field->type == WHOLE && value != (int)value) return 0;
        if (field->type == MULTIPLIER && value != 1 && value != 2 && value != 3 && value != 5 && value != 10)
            return 0;
        out[j] = value;
        while (isspace((unsigned char)*end)) end++;
        if (j + 1 == field->count) return *end == 0;
        if (*end != ',') return 0;
        text = end + 1;
    }
    return 0;
}

int rmc_config_load(FILE *file, char *error, size_t cap)
{
    double parsed[COUNT][WIDTH];
    unsigned char seen[COUNT] = {0};
    const char *section = "", *reason = "";
    char line[1024];
    size_t number = 0;
    for (int i = 0; i < COUNT; i++)
        memcpy(parsed[i], fields[i].defaults, sizeof parsed[i]);

    while (file) {
        int c;
        size_t length = 0;
        number++;
        while ((c = fgetc(file)) != EOF && c != '\n') {
            if (!c || length == sizeof line - 1) {
                reason = c ? "line exceeds 1023 bytes" : "embedded NUL";
                goto fail;
            }
            line[length++] = (char)c;
        }
        if (ferror(file)) { reason = "read error"; goto fail; }
        if (c == EOF && !length) break;
        line[length] = 0;
        char *p = line;
        if (number == 1 && !strncmp(p, "\xef\xbb\xbf", 3)) p += 3;
        p = trim(p);
        if (!*p || *p == ';' || *p == '#') continue;
        if (*p == '[') {
            char *end = strchr(p + 1, ']');
            if (!end || end[1]) { reason = "invalid section"; goto fail; }
            *end = 0;
            p = trim(p + 1);
            section = NULL;
            for (int i = 0; i < COUNT; i++)
                if (equal(p, fields[i].section)) { section = fields[i].section; break; }
            if (!section) { reason = "unknown section"; goto fail; }
        } else {
            char *val = strchr(p, '=');
            if (!val) { reason = "expected key=value"; goto fail; }
            *val++ = 0;
            int i = lookup(section, trim(p));
            if (i < 0) { reason = "unknown key"; goto fail; }
            if (seen[i]) continue;
            if (!parse(fields + i, trim(val), parsed[i])) {
                reason = fields[i].key;
                goto fail;
            }
            seen[i] = 1;
        }
    }
    for (int i = 0; i < COUNT; ++i)
        for (unsigned j = 0; j < fields[i].count; ++j) values[i][j] = parsed[i][j];
    if (cap) error[0] = 0;
    return 1;
fail:
    if (cap) snprintf(error, cap, "rmc.ini line %zu: %s", number, reason);
    return 0;
}

/* The reader accepts repeated sections and uses the first assignment. Preserve
 * comments, line endings and unrelated keys when an older INI lacks an option. */
int rmc_config_edit(FILE *src, FILE *dst, const char *section, const char *key, int value)
{
    int i = lookup(section, key);
    char number[20];
    double checked;
    snprintf(number, sizeof number, "%d", value);
    if (i < 0 || fields[i].count != 1) return 0;
    const char *encoded = fields[i].type == VISIBILITY && (value==0 || value==1) ?
                          (value ? "hide" : "show") : number;
    if (!parse(fields+i, encoded, &checked)) return 0;
    char line[1026], copy[sizeof line];
    int active = 0, replaced = 0, first = 1, newline = 1;
    long offset = ftell(src);
    if (offset < 0) return 0;
    while (fgets(line, sizeof line, src)) {
        size_t len = strlen(line);
        long next = ftell(src);
        if (next < offset || (size_t)(next-offset) != len) return 0;
        offset = next;
        newline = len && line[len-1] == '\n';
        if (!newline && !feof(src)) return 0;
        memcpy(copy, line, len+1);
        char *p = trim(copy + (first && !strncmp(copy,"\xef\xbb\xbf",3) ? 3 : 0));
        first = 0;
        if (*p == '[') {
            char *end = strchr(p, ']');
            if (!end) return 0;
            *end = 0;
            active = equal(trim(p+1), section);
        } else if (active && !replaced && *p != ';' && *p != '#') {
            char *equals = strchr(p, '=');
            if (equals) {
                *equals = 0;
                if (equal(trim(p), key)) {
                    if (fprintf(dst,"%s=%s%s",key,encoded,strstr(line,"\r\n") ? "\r\n" : newline ? "\n" : "") < 0) return 0;
                    replaced = 1;
                    continue;
                }
            }
        }
        if (fputs(line,dst) == EOF) return 0;
    }
    return !ferror(src) && (replaced || fprintf(dst,"%s[%s]\n%s=%s\n",newline ? "" : "\n",section,key,encoded) >= 0);
}

void rmc_config_publish(const char *section, const char *key, int value)
{
    int i = lookup(section,key);
    if (i < 0 || fields[i].count != 1) abort();
    values[i][0] = value;
}
