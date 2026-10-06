#ifndef HERDCAT_AGENT_HOOK_INTERNAL_H
#define HERDCAT_AGENT_HOOK_INTERNAL_H

#include "core/agent_hook.h"

void agent_hook_prompt_byte(agent_hook_scanner_t *s, unsigned char c);
bool digit(unsigned char c);
bool hex(unsigned char c);

#endif
