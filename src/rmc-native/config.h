#ifndef RMC_CONFIG_H
#define RMC_CONFIG_H

#include <stdio.h>

/* Call once during initialization. NULL selects the compiled-in preset.
   A parse error leaves the preceding configuration intact. */
int rmc_config_load(FILE *file, char *error, size_t cap);
double rmc_value(const char *section, const char *key, unsigned index);
int rmc_int(const char *section, const char *key);

int rmc_config_edit(FILE *source, FILE *dest, const char *section, const char *key, int value);
void rmc_config_publish(const char *section, const char *key, int value);

#endif
