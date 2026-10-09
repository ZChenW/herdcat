#ifndef HERDCAT_FUZZ_H
#define HERDCAT_FUZZ_H

// Each harness defines one fuzz_<target>() entry. A libFuzzer build adds
// LLVMFuzzerTestOneInput around it; tests/test_fuzz_regressions.c calls the
// same entry on the seed corpus and on every input that once crashed.

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FUZZ_MAX_INPUT 65536

// A NUL-terminated heap copy, so an overread by one byte is still caught.
static inline char *fuzz_text(const uint8_t *data, size_t size) {
  char *text = malloc(size + 1);
  if (!text)
    abort();
  if (size)
    memcpy(text, data, size);
  text[size] = '\0';
  return text;
}

#ifdef FUZZ_MAIN
#  define FUZZ_ENTRY(name)                                         \
    int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);  \
    int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) { \
      return name(data, size);                                     \
    }
#else
#  define FUZZ_ENTRY(name)
#endif

#endif
