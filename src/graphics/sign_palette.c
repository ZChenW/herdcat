#include "graphics/sign_palette.h"

#include "config/sign_options.h"

static const sign_palette_t LIGHT = {
    .ink = 0xff111827U,
    .paper = 0xfff8fafcU,
    .fills = {0xfff8fafcU, 0xffd9ebffU, 0xffffe4a3U, 0xffc7f1d6U, 0xffffbcaeU},
    .icons = {0xff8b93a1U, 0xff24558fU, 0xff71430bU, 0xff22643dU, 0xff8a2415U},
    .secondary = 0xff4a5261U,
    .hover = 0xffe3e8f0U,
    .count = 0xff4a5261U,
    .plate = 0xffffe4a3U,
    .meta = 0xff71430bU,
};

static const sign_palette_t DARK = {
    .ink = 0xffe6e9efU,
    .paper = 0xff1c2230U,
    .fills = {0xff232a39U, 0xff1e3a5fU, 0xff5a4410U, 0xff1d4a33U, 0xff5c2a22U},
    .icons = {0xff8b93a1U, 0xff8ec5ffU, 0xffffd166U, 0xff7ee2a8U, 0xffff9a8aU},
    .secondary = 0xffa9b1c0U,
    .hover = 0xff2d3648U,
    .count = 0xffa9b1c0U,
    .plate = 0xff5a4410U,
    .meta = 0xffffd166U,
};

const sign_palette_t *sign_palette(sign_theme_t theme) {
  return theme == SIGN_THEME_DARK ? &DARK : &LIGHT;
}

static sign_theme_t system_theme = SIGN_THEME_LIGHT;
sign_theme_t sign_theme_effective(sign_theme_t choice) {
  return choice == SIGN_THEME_AUTO ? system_theme : choice;
}
void sign_theme_system(sign_theme_t theme) {
  system_theme = theme == SIGN_THEME_DARK ? SIGN_THEME_DARK : SIGN_THEME_LIGHT;
}
