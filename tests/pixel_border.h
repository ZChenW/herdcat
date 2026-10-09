#ifndef HERDCAT_TEST_PIXEL_BORDER_H
#define HERDCAT_TEST_PIXEL_BORDER_H

// Skip the interior, which has no border assertions, without skipping any
// exterior pixel. The caller retains its original assertions and order.
static inline int test_border_column(int x, int y, int left, int right,
                                     int bottom) {
  return y >= left && y < bottom && x >= left && x < right ? right : x;
}

#endif
