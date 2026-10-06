#ifndef HERDCAT_JSON_H
#define HERDCAT_JSON_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
  const char *p, *end;
} json_span_t;
bool json_document(const char *text, size_t length, json_span_t *out);
bool json_field(json_span_t s, const char *key, json_span_t *out);
bool json_item(json_span_t s, size_t index, json_span_t *out);
bool json_equal(json_span_t v, const char *text);
bool json_uint(json_span_t v, uint64_t *out);
bool json_text(json_span_t v, char *out, size_t capacity);
#endif
