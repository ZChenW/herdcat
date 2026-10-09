// The setgid helper's command line. Arguments are the input split at NUL
// bytes; the helper must refuse anything but canonical /dev/input/eventN
// paths and in-range numbers before it opens a single device.
#define _GNU_SOURCE
#define main fuzz_input_helper_main
int fuzz_input_helper_main(int argc, char **argv);
// The harness uses the parsing half; capture and seccomp stay unreferenced.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../../src/input/input_helper.c"
#pragma GCC diagnostic pop
#undef main

#include "fuzz.h"

static int fuzz_input(const uint8_t *data, size_t size) {
  if (size > 4096)
    return 0;
  char *text = fuzz_text(data, size);
  char *argv[INPUT_MAX_DEVICES + 8] = {(char *)"herdcat-input"};
  int argc = 1;
  for (size_t at = 0; at <= size && argc < INPUT_MAX_DEVICES + 7;
       at += strlen(text + at) + 1) {
    argv[argc++] = text + at;
    (void)input_event_path(text + at);
    int value;
    (void)input_number(text + at, INPUT_MAX_DEVICES, &value);
  }
  argv[argc] = NULL;
  // The output descriptor is forced past any open one: only parsing runs.
  if (argc > 1)
    argv[1] = (char *)"1048575";
  input_options_t options = {0};
  if (input_arguments(argc, argv, &options))
    abort();  // No such descriptor exists, so acceptance is a bug.
  free(text);
  return 0;
}
FUZZ_ENTRY(fuzz_input)
