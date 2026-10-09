#include "test_helpers.h"
#include "utils/json.h"

#include <stdint.h>
#include <string.h>

static json_span_t span(const char *text) {
  return (json_span_t){text, text + strlen(text)};
}

int main(void) {
  uint64_t number;
  TEST_ASSERT(json_uint(span("18446744073709551615"), &number));
  TEST_ASSERT(number == UINT64_MAX);
  const char *bad_numbers[] = {"18446744073709551616",
                               "999999999999999999999",
                               "01",
                               "-1",
                               "",
                               "1x",
                               "1.0"};
  for (size_t i = 0; i < sizeof(bad_numbers) / sizeof(bad_numbers[0]); i++)
    TEST_ASSERT(!json_uint(span(bad_numbers[i]), &number));
  json_span_t value;
  char *large = malloc(65537);
  TEST_ASSERT(large != NULL);
  memset(large, ' ', 65537);
  large[0] = '0';
  TEST_ASSERT(json_document(large, 65536, &value));
  TEST_ASSERT(!json_document(large, 65537, &value));
  large[1] = '\0';
  TEST_ASSERT(!json_document(large, 2, &value));
  free(large);
  TEST_ASSERT(!json_document(NULL, 0, &value));
  TEST_ASSERT(!json_document("0", 1, NULL));
  char nested[80];
  for (int depth = 32; depth <= 33; depth++) {
    memset(nested, '[', (size_t)depth);
    nested[depth] = '0';
    memset(nested + depth + 1, ']', (size_t)depth);
    TEST_ASSERT(json_document(nested, (size_t)(depth * 2 + 1), &value) ==
                (depth == 32));
  }
  const char *bad_documents[] = {"[1,]",
                                 "{\"k\" 1}",
                                 "{\"k\":}",
                                 "[1 2]",
                                 "[1",
                                 "1e+",
                                 "1.",
                                 "\"\\u123\"",
                                 "\"\\ux000\"",
                                 "\"\\q\"",
                                 "\"bad\nstring\"",
                                 "true false"};
  for (size_t i = 0; i < sizeof(bad_documents) / sizeof(bad_documents[0]); i++)
    TEST_ASSERT(
        !json_document(bad_documents[i], strlen(bad_documents[i]), &value));
  const char *duplicate = "{\"pid\":1,\"pid\":2}";
  TEST_ASSERT(json_document(duplicate, strlen(duplicate), &value));
  json_span_t field;
  TEST_ASSERT(!json_field(value, "pid", &field));
  const char *valid = "{\"pid\":42}";
  TEST_ASSERT(json_document(valid, strlen(valid), &value));
  TEST_ASSERT(json_field(value, "pid", &field));
  TEST_ASSERT(json_uint(field, &number) && number == 42);
  char text[8] = "safe";
  TEST_ASSERT(!json_text(span("\"x\""), text, 0));
  TEST_ASSERT(!strcmp(text, "safe"));
  TEST_ASSERT(!json_text(span("x"), text, sizeof(text)));
  puts("JSON overflow, size, nesting and malformed-input recovery passed.");
  return 0;
}
