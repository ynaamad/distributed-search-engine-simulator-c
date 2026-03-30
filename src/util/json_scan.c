#include "util/json_scan.h"
#include <stdlib.h>
#include <string.h>

// Skip over a JSON value (string, number, array, object, bool, null)
// and return pointer to the character after it.
static const char *skip_value(const char *p) {
    while (*p == ' ') p++;
    if (*p == '"') {
        p++;
        while (*p && *p != '"') {
            if (*p == '\\') p++;
            p++;
        }
        if (*p == '"') p++;
    } else if (*p == '[') {
        int depth = 1;
        p++;
        while (*p && depth > 0) {
            if (*p == '[') depth++;
            else if (*p == ']') depth--;
            else if (*p == '"') { p++; while (*p && *p != '"') { if (*p == '\\') p++; p++; } }
            p++;
        }
    } else if (*p == '{') {
        int depth = 1;
        p++;
        while (*p && depth > 0) {
            if (*p == '{') depth++;
            else if (*p == '}') depth--;
            else if (*p == '"') { p++; while (*p && *p != '"') { if (*p == '\\') p++; p++; } }
            p++;
        }
    } else {
        // number, bool, null — skip to next delimiter
        while (*p && *p != ',' && *p != '}' && *p != ']') p++;
    }
    return p;
}

void json_scan_object(const char *line, JsonFieldCallback cb, void *userdata) {
    const char *p = line;

    // Find opening brace
    while (*p && *p != '{') p++;
    if (!*p) return;
    p++;

    while (*p) {
        // Skip whitespace and commas
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r') p++;
        if (*p == '}' || *p == '\0') break;

        // Expect a key string
        if (*p != '"') break;
        p++;
        const char *key_start = p;
        while (*p && *p != '"') p++;
        int key_len = (int)(p - key_start);
        if (*p == '"') p++;

        // Skip colon and whitespace
        while (*p == ' ' || *p == ':') p++;

        // p now points to the value
        cb(key_start, key_len, p, userdata);

        // Skip the value to find the next key
        p = skip_value(p);
    }
}

double json_parse_number(const char *p) {
    return strtod(p, NULL);
}

int json_parse_string(const char *p, char *out, size_t max_len) {
    if (*p != '"') return 0;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < max_len - 1) {
        if (*p == '\\') { p++; if (!*p) break; }
        out[i++] = *p++;
    }
    out[i] = '\0';
    return (int)i;
}

int json_parse_int_array(const char *p, int *out_arr, int max_items) {
    if (*p != '[') return 0;
    p++;
    int count = 0;
    while (*p && *p != ']' && count < max_items) {
        while (*p == ' ' || *p == ',') p++;
        if (*p == ']') break;
        out_arr[count++] = (int)strtol(p, (char **)&p, 10);
    }
    return count;
}

bool json_is_empty_array(const char *p) {
    if (*p != '[') return false;
    p++;
    while (*p == ' ') p++;
    return *p == ']';
}
