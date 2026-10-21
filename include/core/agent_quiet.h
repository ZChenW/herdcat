#ifndef AGENT_QUIET_H
#define AGENT_QUIET_H

#include <stdbool.h>
#include <stdint.h>

// Sync eligibility and sample only when due. No descriptors or threads.
void agent_quiet_sync(bool enabled, int64_t now_ms);
// Zero means there is no eligible working session to sample.
int64_t agent_quiet_deadline(void);
void agent_quiet_reset(void);
// Event-driven record activity can correct a quiet guess without a timer.
void agent_quiet_record(uint64_t key, int64_t now_ms);
bool agent_quiet_stopped(uint64_t key, uint64_t order, int64_t updated_ms);

#endif
