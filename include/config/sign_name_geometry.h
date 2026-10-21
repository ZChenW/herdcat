#ifndef HERDCAT_SIGN_NAME_GEOMETRY_H
#define HERDCAT_SIGN_NAME_GEOMETRY_H
#include "config/config.h"

#include <math.h>
#include <string.h>
static inline int sign_name_clearance(const config_t *config) {
  bool two = config->sign_nameplate[0]
                 ? strstr(config->sign_nameplate, "\\n") != NULL
                 : config->sign_name_extra == SIGN_EXTRA_ABOVE ||
                       config->sign_name_extra == SIGN_EXTRA_BELOW;
  return config->sign_style == SIGN_STYLE_FAN && two
             ? (int)ceil(11.5 * config->sign_font_size / 13.0 *
                         config->cat_height / 110.0 * 1.2)
             : 0;
}
#endif
