#include "graphics/text.h"
#include "test_helpers.h"

#include <stdio.h>
#include <string.h>
#define W 200
#define H 40
static uint8_t a[W * H * 4], b[W * H * 4];
int main(void) {
  TEST_ASSERT(text_init(NULL) == 0);
  TEST_ASSERT(text_measure("", 13, true) == 0);
  TEST_ASSERT(text_measure("abc", 13, true) > text_measure("ab", 13, true));
  TEST_ASSERT(text_measure("\xff", 13, false) == text_measure("�", 13, false));
  text_draw(a, W, H, 5, 24, "MMMMMMMMMM", 13, true, 0xff111827, 1);
  for (size_t i = 0; i < sizeof(a); i++)
    TEST_ASSERT(a[i] == 0);
  int limit = text_measure("…", 13, true);
  text_draw(a, W, H, 5, 24, "MMMMMMMMMM", 13, true, 0xff111827, limit);
  text_draw(b, W, H, 5, 24, "…", 13, true, 0xff111827, limit);
  TEST_ASSERT(!memcmp(a, b, sizeof(a)));
  TEST_ASSERT(memchr(a, 255, sizeof(a)) != NULL);
  memset(a, 0, sizeof(a));
  text_draw_clip(a, W, H, -5, 20, "wayland 项目 等你批准", 13, false,
                 0x804a5261, 45, (text_clip_t){3, 12, 80, 5});
  bool painted = false;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      uint8_t *p = a + (y * W + x) * 4;
      if (x < 3 || x >= 40 || y < 12 || y >= 17)
        TEST_ASSERT(p[3] == 0);
      TEST_ASSERT(p[0] <= p[3] && p[1] <= p[3] && p[2] <= p[3]);
      if (p[3])
        painted = true;
    }
  TEST_ASSERT(painted);
  if (!text_has_glyph(0x732b, false))
    puts("SKIP CJK: no system Chinese font");
  else
    TEST_ASSERT(text_measure("猫", 13, false) > 0);
  for (int scale = 120; scale <= 240; scale += 30) {
    text_set_scale(scale);
    TEST_ASSERT(text_measure("abc", 13, true) > 0);
    text_draw(a, W, H, 0, 35, "猫项目 …", 11.5f, false, 0xffffffff, 60);
  }
  // Exceed the LRU bound, then revisit cached and fallback glyphs.
  for (unsigned cp = 0x400; cp < 0x650; cp++) {
    char s[] = {(char)(0xc0 | (cp >> 6)), (char)(0x80 | (cp & 63)), 0};
    text_measure(s, 13, true);
  }
  TEST_ASSERT(text_measure("abc猫", 13, true) > 0);
  text_cleanup();
  text_cleanup();
  return 0;
}
