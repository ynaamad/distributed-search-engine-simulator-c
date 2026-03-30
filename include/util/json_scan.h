#ifndef DSE_JSON_SCAN_H
#define DSE_JSON_SCAN_H

#include <stdbool.h>
#include <stddef.h>

// Minimal zero-allocation JSON scanner for flat objects with known keys.
// Single-pass: walks the line once, dispatching values by key.

// Callback for each key-value pair found. `val` points to the first char
// of the value (number, opening quote, or opening bracket).
typedef void (*JsonFieldCallback)(const char *key, int key_len,
                                  const char *val, void *userdata);

// Walk a JSON object line, calling cb for each key-value pair.
void json_scan_object(const char *line, JsonFieldCallback cb, void *userdata);

// Helpers for parsing values at a pointer returned by the callback:
double json_parse_number(const char *p);
int    json_parse_string(const char *p, char *out, size_t max_len);
int    json_parse_int_array(const char *p, int *out_arr, int max_items);
bool   json_is_empty_array(const char *p);

#endif // DSE_JSON_SCAN_H
