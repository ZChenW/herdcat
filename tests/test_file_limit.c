#include "test_helpers.h"

#include <sys/resource.h>

int main(void) {
  struct rlimit limit;
  TEST_ASSERT(getrlimit(RLIMIT_NOFILE, &limit) == 0);
  TEST_ASSERT(limit.rlim_cur <= 1024);
  puts("test runner inherits an ordinary terminal's open-file limit");
  return 0;
}
