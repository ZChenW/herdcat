#ifndef HERDCAT_AGENT_CURSOR_H
#define HERDCAT_AGENT_CURSOR_H

#include <stdbool.h>
#include <stdint.h>

void agent_cursor_sync(bool enabled, int64_t now_ms);
int agent_cursor_timeout(int64_t now_ms);
void agent_cursor_reset(void);

#endif
