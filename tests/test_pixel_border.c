#include "pixel_border.h"
#include "test_helpers.h"

static void assert_columns(int y, const int *expected, int count) {
  int visited = 0;
  for (int x = test_border_column(0, y, 1, 5, 4); x < 6;
       x = test_border_column(x + 1, y, 1, 5, 4)) {
    TEST_ASSERT(visited < count && x == expected[visited]);
    visited++;
  }
  TEST_ASSERT(visited == count);
}

int main(void) {
  const int full[] = {0, 1, 2, 3, 4, 5};
  const int sides[] = {0, 5};
  assert_columns(0, full, 6);
  assert_columns(1, sides, 2);
  assert_columns(3, sides, 2);
  assert_columns(4, full, 6);
  // An empty interior and a full-width interior must remain well defined.
  TEST_ASSERT(test_border_column(2, 2, 2, 2, 4) == 2);
  TEST_ASSERT(test_border_column(0, 1, 0, 6, 4) == 6);
  return 0;
}
