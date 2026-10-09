// Runs every fuzz harness, in an ordinary build, over its seed corpus and
// over each input that once crashed it. See tests/fuzz/fuzz.h.
#include "fuzz/fuzz_config.c"
#include "fuzz/fuzz_hook.c"
#include "fuzz/fuzz_input.c"
#include "fuzz/fuzz_json.c"
#include "fuzz/fuzz_requests.c"
#include "fuzz/fuzz_transcript.c"
#include "test_helpers.h"

#include <dirent.h>
#include <stdio.h>

typedef int (*fuzz_entry_t)(const uint8_t *, size_t);

static int replay(const char *kind, const char *target, fuzz_entry_t entry) {
  char dir[256];
  snprintf(dir, sizeof(dir), "tests/fuzz/%s/%s", kind, target);
  DIR *list = opendir(dir);
  if (!list)
    return 0;
  int count = 0;
  for (struct dirent *item; (item = readdir(list));) {
    if (item->d_name[0] == '.')
      continue;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, item->d_name);
    FILE *file = fopen(path, "rb");
    TEST_ASSERT(file);
    static uint8_t data[FUZZ_MAX_INPUT + 1];
    size_t size = fread(data, 1, sizeof(data), file);
    fclose(file);
    // An exact-size heap copy lets a sanitizer build see any overread.
    uint8_t *copy = malloc(size ? size : 1);
    TEST_ASSERT(copy);
    memcpy(copy, data, size);
    (void)entry(copy, size);
    free(copy);
    count++;
  }
  closedir(list);
  return count;
}

int main(void) {
  static const struct {
    const char *name;
    fuzz_entry_t entry;
  } targets[] = {
      {"config",     fuzz_config    },
      {"hook",       fuzz_hook      },
      {"input",      fuzz_input     },
      {"json",       fuzz_json      },
      {"requests",   fuzz_requests  },
      {"transcript", fuzz_transcript},
  };
  for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) {
    int seeds = replay("corpus", targets[i].name, targets[i].entry);
    int crashes = replay("regressions", targets[i].name, targets[i].entry);
    // Every target ships seeds; an empty directory means a broken checkout.
    TEST_ASSERT(seeds > 0);
    printf("%s: %d seeds, %d regressions\n", targets[i].name, seeds, crashes);
  }
  return 0;
}
