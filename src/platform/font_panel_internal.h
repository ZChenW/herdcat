#ifndef HERDCAT_PLATFORM_FONT_PANEL_INTERNAL_H
#define HERDCAT_PLATFORM_FONT_PANEL_INTERNAL_H

#include "graphics/font_panel.h"

extern font_panel_face_t catalog[FONT_PANEL_CAP];
extern int catalog_count, catalog_lang;
void load_faces(bool english);
void note_recent(const char *family);

#endif
