// utils/json.c: a whole document, then every way of walking it.
#include "fuzz.h"
#include "utils/json.h"

static void fuzz_json_walk(json_span_t span, int depth) {
  static const char *const keys[] = {"session_id", "title", "type", "message",
                                     "content",    "text",  "id",   ""};
  char text[64];
  uint64_t number;
  (void)json_uint(span, &number);
  (void)json_text(span, text, sizeof(text));
  (void)json_text(span, text, 1);
  (void)json_equal(span, "true");
  if (depth >= 6)
    return;
  json_span_t child;
  for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
    if (json_field(span, keys[i], &child))
      fuzz_json_walk(child, depth + 1);
  for (size_t i = 0; i < 4 && json_item(span, i, &child); i++)
    fuzz_json_walk(child, depth + 1);
}

static int fuzz_json(const uint8_t *data, size_t size) {
  if (size > FUZZ_MAX_INPUT)
    return 0;
  char *text = fuzz_text(data, size);
  json_span_t root;
  if (json_document(text, size, &root))
    fuzz_json_walk(root, 0);
  free(text);
  return 0;
}
FUZZ_ENTRY(fuzz_json)
