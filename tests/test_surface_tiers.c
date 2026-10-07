#include "platform/overlay_signs.h"
#include "platform/scale.h"
#include "platform/surface_tiers.h"
#include "platform/wayland.h"
#include "test_helpers.h"

static config_t config = {.cat_height = 110,
                          .overlay_height = 120,
                          .sign_style = SIGN_STYLE_FAN,
                          .sign_max = 10};

static void compact_padding(void) {
  surface_size_t zero = surface_tier_size(&config, 0, 2560, 120);
  surface_size_t small = surface_tier_size(&config, 5, 2560, 120);
  surface_size_t full = surface_tier_size(&config, 10, 2560, 120);
  TEST_ASSERT(zero.width == 198 && zero.height == 136);
  TEST_ASSERT(small.width == 652 && small.height == 308);
  TEST_ASSERT(full.width == 652 && full.height == 401);
}
static void runtime_sizes(void) {
  const struct {
    int cat, capacity, output_width;
    uint32_t scale;
    int opacity, pixel_width, pixel_height;
  } cases[] = {
      {40, 0, 800,  150, 0,   90,   70 },
      {40, 5, 800,  150, 0,   300,  149},
      {40, 5, 640,  180, 0,   357,  179},
      {60, 0, 1024, 240, 0,   216,  140},
      {60, 5, 1024, 240, 0,   712,  314},
      {45, 0, 640,  180, 150, 960,  243},
      {45, 0, 1024, 240, 150, 2048, 324}
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
    config_t local = config;
    local.cat_height = cases[i].cat;
    local.overlay_height = 50;
    local.overlay_opacity = cases[i].opacity;
    surface_size_t size = surface_tier_size(
        &local, cases[i].capacity, cases[i].output_width, cases[i].scale);
    if (scale_size_120(size.height, cases[i].scale) != cases[i].pixel_height)
      fprintf(stderr, "runtime row %zu: height %d, expected %d\n", i,
              scale_size_120(size.height, cases[i].scale),
              cases[i].pixel_height);
    TEST_ASSERT(scale_size_120(size.width, cases[i].scale) ==
                cases[i].pixel_width);
    TEST_ASSERT(scale_size_120(size.height, cases[i].scale) ==
                cases[i].pixel_height);
  }
}

static void selection(void) {
  for (int style = SIGN_STYLE_OFF; style <= SIGN_STYLE_FAN; style++) {
    config.sign_style = (sign_style_t)style;
    for (int maximum = 1; maximum <= 10; maximum++) {
      config.sign_max = maximum;
      for (int count = 0; count <= 32; count++) {
        int limited = count < maximum ? count : maximum;
        int expected = !limited       ? 0
                       : limited <= 5 ? (maximum < 5 ? maximum : 5)
                                      : maximum;
        if (style == SIGN_STYLE_OFF)
          expected = maximum;
        TEST_ASSERT(surface_tier_capacity(&config, count, false) == expected);
        surface_size_t full = surface_tier_size(&config, maximum, 2560, 240);
        surface_size_t small = surface_tier_size(&config, expected, 2560, 240);
        TEST_ASSERT(small.width <= full.width && small.height <= full.height);
        if (style != SIGN_STYLE_OFF && count == 0)
          TEST_ASSERT(small.width < full.width && small.height < full.height);
      }
      config.overlay_opacity = 1;
      TEST_ASSERT(surface_tier_capacity(&config, 0, false) == maximum);
      surface_size_t bar = surface_tier_size(&config, 0, 2560, 120);
      TEST_ASSERT(bar.width == 2560);
      TEST_ASSERT(bar.height == overlay_signs_height(&config));
      config.overlay_opacity = 0;
    }
  }
}
static void timing(void) {
  surface_tiers_t a = {.capacity = 10}, b = {.capacity = 0};
  TEST_ASSERT(surface_tier_update(&a, 5, false, true, 100) == -1);
  TEST_ASSERT(a.shrink_at == 0);
  TEST_ASSERT(surface_tier_update(&a, 5, false, false, 200) == -1);
  TEST_ASSERT(a.shrink_at == 10200);
  TEST_ASSERT(surface_tier_update(&a, 5, false, false, 10199) == -1);
  // Menu, panel, grab and expanded hover all use the same blocking gate.
  for (int blocker = 0; blocker < 4; blocker++) {
    TEST_ASSERT(surface_tier_update(&a, 5, true, false, 10200) == -1);
    TEST_ASSERT(a.shrink_at == 0);
    TEST_ASSERT(surface_tier_update(&a, 5, false, false, 11000) == -1);
  }
  TEST_ASSERT(surface_tier_update(&a, 10, false, false, 12000) == -1);
  TEST_ASSERT(a.shrink_at == 0);
  TEST_ASSERT(surface_tier_update(&a, 5, false, false, 13000) == -1);
  TEST_ASSERT(surface_tier_update(&a, 5, false, false, 23000) == 5);
  TEST_ASSERT(a.capacity == 10 && a.pending);
  TEST_ASSERT(surface_tier_update(&a, 10, false, false, 23001) == -1);
  surface_tier_ready(&a);
  TEST_ASSERT(a.capacity == 5 && !a.pending);
  TEST_ASSERT(surface_tier_update(&a, 10, false, false, 23002) == 10);
  TEST_ASSERT(surface_tier_update(&b, 5, true, true, 100) == 5);
  TEST_ASSERT(a.capacity == 5 && b.capacity == 0);
  // Neither request nor configure alone authorizes model entry; ready does.
  TEST_ASSERT(surface_tier_update(&b, 10, false, false, 101) == -1);
  surface_tier_ready(&b);
  TEST_ASSERT(b.capacity == 5);
  TEST_ASSERT(surface_tier_update(&b, 10, false, false, 102) == 10);
}
static void tier_thresholds(void) {
  TEST_ASSERT(
      surface_tier_vertical(NULL, 0, 1080, 120, false, SIGN_ABOVE, 120, 0)
          .cat_y_in_output == 0);
  const int counts[] = {0, 1, 5, 6, 10};
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++) {
    config.sign_style = (sign_style_t)style;
    for (int maximum = 5; maximum <= 10; maximum += 5) {
      config.sign_max = maximum;
      for (size_t n = 0; n < sizeof(counts) / sizeof(*counts); n++) {
        int capacity = surface_tier_capacity(&config, counts[n], false);
        int threshold = capacity <= 5             ? 180
                        : style == SIGN_STYLE_FAN ? 273
                                                  : 345;
        surface_size_t size = surface_tier_size(&config, capacity, 1920, 120);
        config.overlay_position = POSITION_TOP;
        overlay_vertical_t down =
            surface_tier_vertical(&config, threshold - 1, 1080, size.height,
                                  true, SIGN_ABOVE, 120, capacity);
        TEST_ASSERT(down.orientation == SIGN_BELOW);
        overlay_vertical_t up =
            surface_tier_vertical(&config, threshold, 1080, size.height, true,
                                  SIGN_ABOVE, 120, capacity);
        TEST_ASSERT(up.orientation == SIGN_ABOVE);
        down = surface_tier_vertical(&config, threshold + 23, 1080, size.height,
                                     true, SIGN_BELOW, 120, capacity);
        TEST_ASSERT(down.orientation == SIGN_BELOW);
        up = surface_tier_vertical(&config, threshold + 24, 1080, size.height,
                                   true, SIGN_BELOW, 120, capacity);
        TEST_ASSERT(up.orientation == SIGN_ABOVE);
        // The lower-edge condition uses the very same clearance.
        up = surface_tier_vertical(&config, threshold - 1,
                                   110 + 2 * threshold - 2, size.height, true,
                                   SIGN_BELOW, 120, capacity);
        TEST_ASSERT(up.orientation == SIGN_ABOVE);
      }
    }
  }
}
static void tier_flip(void) {
  config.sign_style = SIGN_STYLE_FAN;
  config.sign_max = 10;
  config.overlay_position = POSITION_TOP;
  surface_tiers_t tiers = {.capacity = 5};
  surface_size_t small = surface_tier_size(&config, 5, 1920, 120);
  surface_size_t large = surface_tier_size(&config, 10, 1920, 120);
  overlay_vertical_t p = surface_tier_vertical(&config, 220, 1080, small.height,
                                               true, SIGN_ABOVE, 120, 5);
  TEST_ASSERT(p.orientation == SIGN_ABOVE && p.position_y == 220);
  TEST_ASSERT(surface_tier_update(&tiers, 10, false, false, 100) == 10);
  TEST_ASSERT(tiers.capacity == 5);  // no sixth entry until buffers are ready
  surface_tier_ready(&tiers);
  p = surface_tier_vertical(&config, 220, 1080, large.height, true,
                            p.orientation, 120, tiers.capacity);
  TEST_ASSERT(p.orientation == SIGN_BELOW && p.position_y == 220);
  // During a grab, shrink is blocked and the orientation threshold is stable.
  TEST_ASSERT(surface_tier_update(&tiers, 5, true, false, 200) == -1);
  TEST_ASSERT(tiers.capacity == 10 && !tiers.shrink_at);
  TEST_ASSERT(surface_tier_update(&tiers, 5, false, false, 300) == -1);
  TEST_ASSERT(surface_tier_update(&tiers, 5, false, false, 10299) == -1);
  TEST_ASSERT(surface_tier_update(&tiers, 5, false, false, 10300) == 5);
  surface_tier_ready(&tiers);
  p = surface_tier_vertical(&config, 220, 1080, small.height, true,
                            p.orientation, 120, tiers.capacity);
  TEST_ASSERT(p.orientation == SIGN_ABOVE && p.position_y == 220);
  // Shrinking below the return boundary still stays below.
  p = surface_tier_vertical(&config, 203, 1080, small.height, true, SIGN_BELOW,
                            120, 5);
  TEST_ASSERT(p.orientation == SIGN_BELOW);
}
static void placement(void) {
  config.sign_max = 10;
  const uint32_t scales[] = {120, 240, 150};
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++) {
    config.sign_style = (sign_style_t)style;
    for (int top = 0; top < 2; top++) {
      config.overlay_position = top ? POSITION_TOP : POSITION_BOTTOM;
      for (size_t s = 0; s < sizeof(scales) / sizeof(*scales); s++) {
        for (int target = 0; target <= 950; target += 19) {
          surface_size_t full = surface_tier_size(&config, 10, 2560, scales[s]);
          int base = 1080 - full.height +
                     overlay_signs_resting_y(&config, full.height);
          int position = top ? target : base - target;
          overlay_vertical_t old =
              surface_tier_vertical(&config, position, 1080, full.height, false,
                                    SIGN_ABOVE, scales[s], 10);
          for (int tier = 0; tier <= 10; tier += 5) {
            surface_size_t small =
                surface_tier_size(&config, tier, 2560, scales[s]);
            overlay_vertical_t next =
                surface_tier_vertical(&config, position, 1080, small.height,
                                      false, SIGN_ABOVE, scales[s], tier);
            TEST_ASSERT(old.cat_y_in_output == next.cat_y_in_output);
            TEST_ASSERT(next.position_y == position);
            int threshold = tier <= 5                 ? 180
                            : style == SIGN_STYLE_FAN ? 273
                                                      : 345;
            TEST_ASSERT(next.orientation ==
                        (target >= threshold || target + 110 + threshold > 1080
                             ? SIGN_ABOVE
                             : SIGN_BELOW));
            int origin1 =
                top ? old.margin_y : 1080 - full.height - old.margin_y;
            int origin2 =
                top ? next.margin_y : 1080 - small.height - next.margin_y;
            // Check cat, lifted cat, desk top and displaced desk in pixels.
            const int offsets[] = {0, -8, 68, 78};
            for (size_t i = 0; i < sizeof(offsets) / sizeof(*offsets); i++) {
              TEST_ASSERT(
                  scale_offset_120(origin1, scales[s]) +
                      scale_offset_120(old.cat_y_in_surface + offsets[i],
                                       scales[s]) ==
                  scale_offset_120(origin2, scales[s]) +
                      scale_offset_120(next.cat_y_in_surface + offsets[i],
                                       scales[s]));
            }
          }
        }
      }
    }
  }
}
int main(void) {
  compact_padding();
  runtime_sizes();
  selection();
  timing();
  tier_thresholds();
  tier_flip();
  placement();
  puts("Surface tier selection, delay/cancel/blockers and pixel placement "
       "passed.");
  return 0;
}
