#ifndef HERDCAT_SIGN_NAMES_H
#define HERDCAT_SIGN_NAMES_H
#include "graphics/signs.h"
void sign_title_truncate(const char *title, int limit,
                         char out[AGENT_TITLE_MAX + 4]);
void sign_session_name(const sign_input_t *in, const agent_session_view_t *s,
                       char out[128], bool *title_main);
#endif
