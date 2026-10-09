#define _POSIX_C_SOURCE 200809L
#include "../src/config/config_internal.h"
#include "test_helpers.h"

#include <string.h>

void *__wrap_malloc(size_t size);
void *__real_malloc(size_t size);
void *__wrap_realloc(void *pointer, size_t size);
void *__real_realloc(void *pointer, size_t size);
char *__wrap_strdup(const char *text);
char *__real_strdup(const char *text);

static bool fail_malloc, fail_realloc;
static int strdup_until_failure = -1;

void *__wrap_malloc(size_t size) {
  return fail_malloc ? NULL : __real_malloc(size);
}
void *__wrap_realloc(void *pointer, size_t size) {
  return fail_realloc ? NULL : __real_realloc(pointer, size);
}
char *__wrap_strdup(const char *text) {
  if (strdup_until_failure == 0)
    return NULL;
  if (strdup_until_failure > 0)
    strdup_until_failure--;
  return __real_strdup(text);
}

int main(void) {
  config_t config = {0};
  TEST_ASSERT(config_add_keyboard_device(&config, "/dev/input/event0") ==
              HERDCAT_SUCCESS);
  for (int allocation = 0; allocation < 2; allocation++) {
    fail_realloc = allocation == 0;
    fail_malloc = allocation == 1;
    TEST_ASSERT(config_add_keyboard_device(&config, "/dev/input/event1") ==
                HERDCAT_ERROR_MEMORY);
    fail_realloc = fail_malloc = false;
    TEST_ASSERT(config.num_keyboard_devices == 1);
    TEST_ASSERT(!strcmp(config.keyboard_devices[0], "/dev/input/event0"));
  }
  // A later successful append still works after either allocation failure.
  TEST_ASSERT(config_add_keyboard_device(&config, "/dev/input/event2") ==
              HERDCAT_SUCCESS);
  TEST_ASSERT(config.num_keyboard_devices == 2);
  config_cleanup_full(&config);

  for (int allocation = 0; allocation < 4; allocation++) {
    memset(&config, 0, sizeof(config));
    fail_realloc = allocation == 0;
    fail_malloc = allocation == 1;
    strdup_until_failure = allocation >= 2 ? allocation - 2 : -1;
    TEST_ASSERT(config_parse_key_value(&config, "monitor", "TEST-1,TEST-2") ==
                HERDCAT_ERROR_MEMORY);
    fail_realloc = fail_malloc = false;
    strdup_until_failure = -1;
    config_cleanup_full(&config);
    TEST_ASSERT(config.output_names == NULL && config.output_name == NULL);
    TEST_ASSERT(config.num_output_names == 0);
  }
  puts("Configuration allocation failures preserve arrays and clean up.");
  return 0;
}
