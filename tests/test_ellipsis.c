#include "config/nameplate.h"
#include "graphics/nameplate_layout.h"
#include "graphics/post_text_layout.h"
#include "graphics/text.h"
#include "test_helpers.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define W    256
#define H    96
#define BASE 64
static uint8_t ink[W * H * 4], clipped[W * H * 4];
static bool missing_period;
FT_UInt __real_FT_Get_Char_Index(FT_Face face, FT_ULong cp);
FT_UInt __wrap_FT_Get_Char_Index(FT_Face face, FT_ULong cp);
FT_UInt __wrap_FT_Get_Char_Index(FT_Face face, FT_ULong cp) {
  return missing_period && cp == '.' ? 0 : __real_FT_Get_Char_Index(face, cp);
}
static void no_space_before_marker(void) {
  TEST_ASSERT(text_init("DejaVu Sans") == 0);
  memset(ink, 0, sizeof(ink));
  memset(clipped, 0, sizeof(clipped));
  int budget = text_measure("ABC ", 13, false) + text_measure("…", 13, false);
  text_draw(ink, W, H, 0, BASE, "ABC DEF GHI", 13, false, 0xffffffff, budget);
  text_draw(clipped, W, H, 0, BASE, "ABC…", 13, false, 0xffffffff, budget);
  TEST_ASSERT(!memcmp(ink, clipped, sizeof(ink)));
  // ASCII spaces before stored markers are removed; spacing is geometric.
  TEST_ASSERT(text_measure("ABC   … · DEF", 13, false) ==
              text_measure("ABC… · DEF", 13, false));
  memset(ink, 0, sizeof(ink));
  memset(clipped, 0, sizeof(clipped));
  text_draw(ink, W, H, 0, BASE, "ABC   … · DEF", 13, false, 0xffffffff, 0);
  text_draw(clipped, W, H, 0, BASE, "ABC… · DEF", 13, false, 0xffffffff, 0);
  TEST_ASSERT(!memcmp(ink, clipped, sizeof(ink)));
  text_cleanup();
}
static void baseline(const char *family, bool bold, int scale, float px) {
  TEST_ASSERT(text_init(family) == 0);
  text_set_scale(scale);
  memset(ink, 0, sizeof(ink));
  text_draw(ink, W, H, 0, BASE, "x", px, bold, 0xffffffff, 0);
  int x_top = BASE;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++)
      if (ink[(y * W + x) * 4 + 3] > 32 && y < x_top)
        x_top = y;
  TEST_ASSERT(x_top < BASE);
  memset(ink, 0, sizeof(ink));
  int width = text_measure("…", px, bold);
  int physical = (width * scale + 119) / 120;
  text_draw(ink, W, H, 0, BASE, "…", px, bold, 0xffffffff, 0);
  int top = H, bottom = -1, right = -1, columns = 0;
  bool previous = false;
  for (int x = 0; x < W; x++) {
    bool painted = false;
    for (int y = 0; y < H; y++) {
      if (!ink[(y * W + x) * 4 + 3])
        continue;
      painted = true;
      if (y < top)
        top = y;
      if (y > bottom)
        bottom = y;
      right = x;
    }
    columns += painted && !previous;
    previous = painted;
  }
  printf("%s bold=%d scale=%d px=%.1f missing=%d: width=%d "
         "ink=%d..%d right=%d groups=%d\n",
         family ? family : "default", bold, scale, (double)px, missing_period,
         physical, top, bottom, right, columns);
  TEST_ASSERT(abs(bottom - BASE) <= 1);
  TEST_ASSERT(top > BASE - (BASE - x_top) / 2);
  TEST_ASSERT(columns == 3);
  TEST_ASSERT(right < physical && physical - right <= (scale + 119) / 120);
  memset(clipped, 0, sizeof(clipped));
  text_draw(clipped, W, H, 0, BASE, "…", px, bold, 0xffffffff, physical);
  TEST_ASSERT(!memcmp(ink, clipped, sizeof(ink)));
  memset(clipped, 0, sizeof(clipped));
  text_draw_family(clipped, W, H, 0, BASE, family, "…", px, bold, 0xffffffff,
                   physical);
  TEST_ASSERT(!memcmp(ink, clipped, sizeof(ink)));
  // Automatic truncation must use exactly the explicit marker's ink/width.
  memset(clipped, 0, sizeof(clipped));
  text_draw(clipped, W, H, 0, BASE, "MMMMMMMMMMMMMMMM", px, bold, 0xffffffff,
            physical);
  TEST_ASSERT(!memcmp(ink, clipped, sizeof(ink)));
  // A narrower budget omits the marker rather than clipping its third dot.
  memset(clipped, 0, sizeof(clipped));
  text_draw(clipped, W, H, 0, BASE, "MMMMMMMMMMMMMMMM", px, bold, 0xffffffff,
            right);
  TEST_ASSERT(!memcmp(clipped, (uint8_t[W * H * 4]){0}, sizeof(clipped)));
  // Literal middle-dot separator keeps the font's ordinary, higher position.
  memset(clipped, 0, sizeof(clipped));
  text_draw_family(clipped, W, H, 0, BASE, family, " · ", px, bold, 0xffffffff,
                   0);
  int separator_bottom = -1;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++)
      if (clipped[(y * W + x) * 4 + 3])
        separator_bottom = y;
  TEST_ASSERT(separator_bottom < top);
  // Clip and alpha handling also apply to the synthetic missing-dot path.
  memset(clipped, 0, sizeof(clipped));
  text_draw_clip_family(clipped, W, H, -2, BASE, family, "…", px, bold,
                        0x80806040, physical, (text_clip_t){1, BASE - 2, 3, 2});
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      const uint8_t *p = clipped + (y * W + x) * 4;
      if (x < 1 || x >= 4 || y < BASE - 2 || y >= BASE)
        TEST_ASSERT(p[3] == 0);
      TEST_ASSERT(p[0] <= p[3] && p[1] <= p[3] && p[2] <= p[3]);
    }
  text_cleanup();
}
// Observe the rendered gap, independently of the marker width calculation.
static void ink_gap(const char *family, bool bold, int scale, float px) {
  TEST_ASSERT(text_init(family) == 0);
  text_set_scale(scale);
  memset(ink, 0, sizeof(ink));
  text_draw(ink, W, H, 0, BASE, "a", px, bold, 0xffffffff, 0);
  int last = -1;
  for (int x = 0; x < W; x++)
    for (int y = 0; y < H; y++)
      if (ink[(y * W + x) * 4 + 3])
        last = x;
  TEST_ASSERT(last >= 0);
  memset(clipped, 0, sizeof(clipped));
  text_draw(clipped, W, H, 0, BASE, "a… · a", px, bold, 0xffffffff, 0);
  int first = W;
  for (int x = last + 1; x < W && first == W; x++)
    for (int y = 0; y < H; y++)
      if (clipped[(y * W + x) * 4 + 3])
        first = x;
  int minimum = (int)floor((double)px * scale / 120.0 * .08);
  if (minimum < 1)
    minimum = 1;
  printf("gap %s bold=%d scale=%d px=%.1f: blank=%d minimum=%d last=%d "
         "first=%d advance=%d\n",
         family, bold, scale, (double)px, first - last - 1, minimum, last,
         first, text_measure("a", px, bold));
  TEST_ASSERT(first < W && first - last - 1 >= minimum);
  // The standalone marker starts at the clamped bearing, not a space glyph.
  memset(ink, 0, sizeof(ink));
  text_draw(ink, W, H, 0, BASE, "…", px, bold, 0xffffffff, 0);
  first = W;
  for (int x = 0; x < W && first == W; x++)
    for (int y = 0; y < H; y++)
      if (ink[(y * W + x) * 4 + 3])
        first = x;
  int maximum = (int)floor((double)px * scale / 120.0 * .28);
  if (maximum < 1)
    maximum = 1;
  TEST_ASSERT(first >= minimum && first <= maximum);
  text_cleanup();
}
static void budgets(const char *family) {
  TEST_ASSERT(text_init(family) == 0);
  sign_text_t text = {.px = 13, .meta_px = 11.5, .gap = 7, .templated = true};
  nameplate_fields_t fields = {"herdcat", "herdcat",
                               "Project optimization and 中文截断预算检查",
                               "Claude + Codex", "等待子代理 15 分钟"};
  nameplate_expand("**{name}**  {title} · {agent} · {state}", &fields,
                   &text.nameplate);
  for (int budget = 24; budget < 500; budget++) {
    text.max_width = budget;
    nameplate_layout_t layout;
    nameplate_layout(&text, 1, &layout);
    TEST_ASSERT(layout.width <= budget - 24);
    double used = 0;
    for (int i = 0; i < layout.count; i++) {
      const nameplate_paint_run_t *r = &layout.runs[i];
      if (!r->gap)
        TEST_ASSERT(r->width == text_measure(r->text, r->px, r->bold));
      used += r->width;
    }
    TEST_ASSERT(used == layout.width);
  }
  strcpy(text.value, "herdcat");
  strcpy(text.extra, fields.title);
  strcpy(text.meta, "Claude · 15 min");
  for (int reverse = 0; reverse < 2; reverse++) {
    text.reverse = reverse;
    bool shortened = false;
    for (int budget = 150; budget < 380; budget++) {
      text.w = budget;
      post_text_layout_t layout;
      post_text_layout(&text, &layout);
      TEST_ASSERT(layout.extra_width ==
                  text_measure(layout.extra, 11.5, false));
      TEST_ASSERT(layout.extra_x >= 0 &&
                  layout.extra_x + layout.extra_width <= budget);
      double main =
          fmin(text_measure(text.value, 13, true), layout.name_budget);
      double meta = text_measure(text.meta, 11.5, false);
      TEST_ASSERT(main + meta + layout.extra_width <= budget);
      shortened |= strstr(layout.extra, "…") != NULL;
    }
    TEST_ASSERT(shortened);
  }
  text_cleanup();
}
static void family_override(void) {
  const char *sample = "ABC   … · 中文";
  TEST_ASSERT(text_init("DejaVu Sans") == 0);
  text_set_scale(210);
  for (int bold = 0; bold < 2; bold++) {
    memset(ink, 0, sizeof(ink));
    text_draw_family(ink, W, H, 0, BASE, "Noto Sans CJK SC", sample, 13, bold,
                     0xffffffff, 0);
    int width = text_measure_family("Noto Sans CJK SC", sample, 13, bold);
    TEST_ASSERT(text_set_family("Noto Sans CJK SC") == 0);
    TEST_ASSERT(text_measure(sample, 13, bold) == width);
    memset(clipped, 0, sizeof(clipped));
    text_draw(clipped, W, H, 0, BASE, sample, 13, bold, 0xffffffff, 0);
    TEST_ASSERT(!memcmp(ink, clipped, sizeof(ink)));
    TEST_ASSERT(text_set_family("DejaVu Sans") == 0);
  }
  text_cleanup();
}
static void no_font(void) {
  // Existing shape-only goldens must never gain synthetic dots.
  memset(ink, 0, sizeof(ink));
  TEST_ASSERT(text_measure("…", 13, false) == 0);
  text_draw(ink, W, H, 0, BASE, "… · …", 13, false, 0xffffffff, 0);
  TEST_ASSERT(!memcmp(ink, (uint8_t[W * H * 4]){0}, sizeof(ink)));
}
int main(int argc, char **argv) {
  no_font();
  no_space_before_marker();
  const char *families[] = {"Noto Serif CJK HK", "Noto Sans CJK SC",
                            "DejaVu Sans", "JetBrains Mono", NULL};
  for (int f = 0; f < 4; f++)
    for (int bold = 0; bold < 2; bold++)
      for (int scale = 120; scale <= 240; scale += 30)
        ink_gap(families[f], bold, scale, 13);
  const float sizes[] = {10, 11.5f, 13, 15, 20};
  for (int missing = argc > 1 && !strcmp(argv[1], "missing"); missing < 2;
       missing++) {
    missing_period = missing;
    for (int f = 0; f < 5; f++)
      for (int bold = 0; bold < 2; bold++)
        for (int scale = 120; scale <= 240; scale += 30)
          for (size_t px = 0; px < sizeof(sizes) / sizeof(sizes[0]); px++)
            baseline(families[f], bold, scale, sizes[px]);
    for (int f = 0; f < 5; f++)
      budgets(families[f]);
    family_override();
  }
  no_font();
  return 0;
}
