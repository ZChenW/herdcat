#include "graphics/text.h"
#include "test_helpers.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#define W 200
#define H 40
static uint8_t a[W * H * 4], b[W * H * 4];
static void test_metrics(void) {
  text_metrics_t metrics;
  TEST_ASSERT(text_init("Noto Sans") == 0);
  TEST_ASSERT(text_metrics(13, true, &metrics));
  TEST_ASSERT(metrics.ascent > 9 && metrics.ascent < 18);
  TEST_ASSERT(metrics.descent > 1 && metrics.descent < 8);
  TEST_ASSERT(metrics.ascent > metrics.descent);
  double line = 15.6, top = 40;
  double expect = top + line / 2 + metrics.cap_height / 2;
  TEST_ASSERT(fabs(text_baseline(top, line, 13, true) - expect) < 1e-9);
  text_metrics_t scaled;
  text_set_scale(240);
  TEST_ASSERT(text_metrics(13, true, &scaled));
  TEST_ASSERT(fabs(scaled.ascent - metrics.ascent) < 0.05);
  TEST_ASSERT(fabs(scaled.descent - metrics.descent) < 0.05);
  text_set_scale(120);
  text_measure("猫项目", 13, true);
  text_metrics_t after;
  TEST_ASSERT(text_metrics(13, true, &after));
  TEST_ASSERT(after.ascent == metrics.ascent &&
              after.descent == metrics.descent);
  text_metrics_t blank;
  TEST_ASSERT(!text_metrics(0, true, &blank));
  TEST_ASSERT(!text_metrics(13, true, NULL));
  text_cleanup();
  TEST_ASSERT(!text_metrics(13, true, &metrics));
  TEST_ASSERT(text_baseline(3, 10, 13, true) == 3);
}
static void test_family(void) {
  static const char *const extra[] = {
      "Noto Serif",
      "Noto Sans Mono",
      "Noto Sans CJK SC",
      "DejaVu Sans",
      "Liberation Mono",
      "Noto Serif Tamil ExtraCondensed",
      "Noto Sans Malayalam SemiCondensed",
      "Noto Sans Kannada UI",
      "Noto Sans Lao ExtraCondensed",
      "Noto Serif Display ExtraCondensed",
      "Noto Sans Gurmukhi UI Condensed",
      "Noto Sans Devanagari",
      "Noto Sans Telugu UI",
      "Noto Sans Oriya",
      "Noto Serif Tamil SemiCondensed",
      "Noto Serif Myanmar SemiCondensed",
      "Noto Sans Arabic UI",
      "Noto Sans Gunjala Gondi",
      "Noto Sans Gujarati",
      "Noto Sans Gujarati UI",
      "Noto Sans Devanagari UI SemiCondensed",
      "Noto Sans Georgian",
      "Noto Serif Display Condensed",
      "Noto Serif NP Hmong",
      "Noto Sans Hebrew",
      "Noto Sans Sinhala Condensed",
      "Noto Sans Syriac",
      "Noto Serif Khmer",
      "Noto Sans Myanmar SemiCondensed",
      "Noto Sans Malayalam ExtraCondensed",
      "Noto Sans Malayalam",
      "Noto Sans Telugu",
      "Noto Sans Khmer",
      "Noto Serif Georgian",
      "Noto Sans Armenian",
      "Noto Sans Gurmukhi UI ExtraCondensed",
      "Noto Serif Myanmar ExtraCondensed",
      "Noto Sans Bengali",
      "Noto Sans Mahajani",
      "Noto Sans Tamil ExtraCondensed",
      "Noto Sans Thaana",
      "Noto Serif Tibetan",
      "Noto Serif Myanmar",
      "Noto Sans Malayalam UI Condensed",
      "Noto Sans Devanagari UI ExtraCondensed",
      "Noto Sans Khojki",
      "Noto Sans Warang Citi",
      "Noto Serif Devanagari",
      "Noto Serif Tamil",
      "Noto Sans Arabic",
      "Noto Sans Thai Looped",
      "Noto Serif Telugu",
      "Noto Sans Myanmar Condensed",
      "Noto Sans Thai",
      "Noto Serif Gujarati",
      "Noto Serif Bengali",
      "Noto Sans Kannada",
      "Noto Serif Hebrew",
      "Noto Sans Zanabazar Square",
      "Noto Serif Sinhala Condensed",
      "Noto Sans Carian",
      "Noto Sans Bassa Vah",
      "Noto Sans Gurmukhi UI SemiCondensed",
      "Noto Serif Armenian",
      "Noto Serif Sinhala",
      "Noto Serif Display",
      "Noto Sans Lao Looped Condensed",
      "Noto Serif Lao SemiCondensed",
      "Noto Sans Mayan Numerals",
      "Noto Serif Oriya",
      "Noto Sans Malayalam Condensed",
      "Noto Sans Psalter Pahlavi",
      "Noto Rashi Hebrew",
      "Noto Sans Lao Condensed",
  };
  TEST_ASSERT(text_set_family("Noto Sans") == -1);
  text_metrics_t before;
  TEST_ASSERT(text_init("Noto Sans") == 0);
  TEST_ASSERT(text_metrics(13, false, &before));
  int wayland = text_measure("Wayland", 13, true);
  TEST_ASSERT(text_measure_family("Noto Serif", "Wayland", 13, true) > 0);
  text_metrics_t still;
  TEST_ASSERT(text_metrics(13, false, &still));
  TEST_ASSERT(still.ascent == before.ascent && still.descent == before.descent);
  TEST_ASSERT(text_measure("Wayland", 13, true) == wayland);
  TEST_ASSERT(text_measure("M", 16, false) > text_measure("i", 16, false));
  TEST_ASSERT(text_set_family("Noto Sans Mono") == 0);
  TEST_ASSERT(text_measure("i", 16, false) == text_measure("M", 16, false));
  TEST_ASSERT(text_set_family(NULL) == 0);
  TEST_ASSERT(text_measure("M", 16, false) > text_measure("i", 16, false));
  TEST_ASSERT(text_set_family("Noto Sans") == 0);
  memset(a, 0, sizeof(a));
  memset(b, 0, sizeof(b));
  text_draw(a, W, H, 4, 28, "Ag", 18, false, 0xff111827, 0);
  text_draw_family(b, W, H, 4, 28, "Noto Serif", "Ag", 18, false, 0xff111827,
                   0);
  TEST_ASSERT(memcmp(a, b, sizeof(a)) != 0);
  TEST_ASSERT(text_set_family("Noto Serif") == 0);
  memset(a, 0, sizeof(a));
  text_draw(a, W, H, 4, 28, "Ag", 18, false, 0xff111827, 0);
  TEST_ASSERT(memcmp(a, b, sizeof(a)) == 0);
  TEST_ASSERT(text_set_family("Noto Sans CJK SC") == 0);
  TEST_ASSERT(text_measure("项目", 13, false) > 0);
  text_metrics_t cjk;
  TEST_ASSERT(text_metrics(13, true, &cjk));
  TEST_ASSERT(cjk.ascent > 8 && cjk.ascent < 22);
  TEST_ASSERT(cjk.descent >= 0 && cjk.descent < 10);
  TEST_ASSERT(text_set_family("Noto Sans") == 0);
  for (int size = 8; size <= 36; size++) {
    for (char c = 'A'; c <= 'Z'; c++) {
      char s[2] = {c, 0};
      text_measure(s, (float)size, false);
    }
  }
  TEST_ASSERT(text_glyph_count() == 512);
  int opened = 0;
  for (size_t i = 0; i < sizeof(extra) / sizeof(extra[0]); i++) {
    text_metrics_t metrics;
    if (text_metrics_family(extra[i], 13, false, &metrics))
      opened++;
  }
  TEST_ASSERT(opened >= 70);
  TEST_ASSERT(text_set_family("Liberation Mono") == 0);
  TEST_ASSERT(text_glyph_count() <= 512);
  TEST_ASSERT(text_glyph_count() > 256);
  text_cleanup();
  TEST_ASSERT(text_glyph_count() == 0);
  TEST_ASSERT(text_set_family(NULL) == -1);
  memset(a, 0, sizeof(a));
  memset(b, 0, sizeof(b));
}
static void test_catalog(void) {
  const char *probe = NULL;
  TEST_ASSERT(text_families("en", &probe, 1) == -1);
  TEST_ASSERT(text_families_class("en", TEXT_CLASS_ALL, NULL, 0) == -1);
  TEST_ASSERT(text_family_spacing("Noto Sans") == -2);
  TEST_ASSERT(text_init("Noto Sans") == 0);
  const char *en[512];
  const char *zh[512];
  int en_n = text_families("en", en, 512);
  int zh_n = text_families("zh-cn", zh, 512);
  TEST_ASSERT(en_n > 0 && zh_n > 0 && en_n <= 512 && zh_n <= 512);
  for (int i = 1; i < en_n; i++)
    TEST_ASSERT(strcmp(en[i - 1], en[i]) < 0);
  for (int i = 1; i < zh_n; i++)
    TEST_ASSERT(strcmp(zh[i - 1], zh[i]) < 0);
  const char *out = (const char *)1;
  int index = text_family_step("en", NULL, 0, &out);
  TEST_ASSERT(index == 0 && out == NULL);
  index = text_family_step("en", "", 1, &out);
  TEST_ASSERT(index == 1 && out && !strcmp(out, en[0]));
  index = text_family_step("en", en[en_n - 1], 1, &out);
  TEST_ASSERT(index == 0 && out == NULL);
  index = text_family_step("en", NULL, -1, &out);
  TEST_ASSERT(index == en_n && out && !strcmp(out, en[en_n - 1]));
  const char *missing = "Not A Real Family";
  index = text_family_step("en", missing, 0, &out);
  TEST_ASSERT(index == -1 && out == missing);
  index = text_family_step("en", missing, 1, &out);
  TEST_ASSERT(index == 0 && out == NULL);
  index = text_family_step("en", missing, -1, &out);
  TEST_ASSERT(index == en_n && out && !strcmp(out, en[en_n - 1]));
  if (en_n > 1) {
    index = text_family_step("en", en[0], 1, &out);
    TEST_ASSERT(index == 2 && !strcmp(out, en[1]));
  }
  int cached = text_families("en", NULL, 0);
  TEST_ASSERT(cached == en_n);
  text_cleanup();
  TEST_ASSERT(text_families("en", NULL, 0) == cached);
  memset(a, 0, sizeof(a));
  memset(b, 0, sizeof(b));
}
static bool listed(const char **names, int count, const char *name) {
  for (int i = 0; i < count; i++)
    if (!strcmp(names[i], name))
      return true;
  return false;
}
static void test_class(void) {
  TEST_ASSERT(!text_spacing_mono(-1));
  TEST_ASSERT(!text_spacing_mono(0));
  TEST_ASSERT(!text_spacing_mono(89));
  TEST_ASSERT(text_spacing_mono(90));
  TEST_ASSERT(text_spacing_mono(100));
  TEST_ASSERT(text_spacing_mono(110));
  TEST_ASSERT(text_init("Noto Sans") == 0);
  const char *all[512], *prop[512], *mono[512];
  int all_n = text_families_class("en", TEXT_CLASS_ALL, all, 512);
  int prop_n = text_families_class("en", TEXT_CLASS_PROP, prop, 512);
  int mono_n = text_families_class("en", TEXT_CLASS_MONO, mono, 512);
  TEST_ASSERT(all_n > 0 && prop_n >= 0 && mono_n > 0);
  TEST_ASSERT(all_n == prop_n + mono_n);
  TEST_ASSERT(text_families_class("en", (text_face_class_t)9, NULL, 0) == -1);
  for (int i = 0; i < all_n; i++) {
    int spacing = text_family_spacing(all[i]);
    bool is_mono = text_spacing_mono(spacing);
    TEST_ASSERT(listed(mono, mono_n, all[i]) == is_mono);
    TEST_ASSERT(listed(prop, prop_n, all[i]) == !is_mono);
  }
  text_cleanup();
}
// Every glyph used to ask Fontconfig which face draws it, on every draw, even
// when the glyph was already cached. A panel of font names took most of a
// second per redraw. Drawing the same text again must not ask again.
static void test_routes(void) {
  static uint8_t pixels[120 * 40 * 4];
  TEST_ASSERT(text_init(NULL) == 0);
  const char *mixed = "wayland 项目 等你批准";
  text_draw(pixels, 120, 40, 2, 24, mixed, 13, true, 0xff111827, 110);
  text_draw_family(pixels, 120, 40, 2, 24, "monospace", mixed, 13, true,
                   0xff111827, 110);
  TEST_ASSERT(text_measure_family("monospace", mixed, 13, true) > 0);
  int asked = text_match_count();
  for (int frame = 0; frame < 20; frame++) {
    text_draw(pixels, 120, 40, 2, 24, mixed, 13, true, 0xff111827, 110);
    text_draw_family(pixels, 120, 40, 2, 24, "monospace", mixed, 13, true,
                     0xff111827, 110);
    TEST_ASSERT(text_measure(mixed, 13, true) > 0);
    TEST_ASSERT(text_measure_family("monospace", mixed, 13, true) > 0);
  }
  TEST_ASSERT(text_match_count() == asked);
  // A family's own face answers for the glyphs it has: first use of a family
  // costs one lookup, not one per code point.
  int before = text_match_count();
  TEST_ASSERT(text_measure_family("serif", "abcdefghijklmnop", 13, true) > 0);
  TEST_ASSERT(text_match_count() - before <= 2);
  // Switching the main font keeps earlier answers usable and still draws.
  TEST_ASSERT(text_set_family("monospace") == 0);
  TEST_ASSERT(text_measure(mixed, 13, true) > 0);
  int after_switch = text_match_count();
  TEST_ASSERT(text_measure(mixed, 13, true) > 0);
  TEST_ASSERT(text_match_count() == after_switch);
  text_cleanup();
}
int main(void) {
  test_routes();
  test_metrics();
  test_family();
  test_catalog();
  test_class();
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
