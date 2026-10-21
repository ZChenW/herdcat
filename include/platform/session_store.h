#ifndef SESSION_STORE_H
#define SESSION_STORE_H

#include <stdbool.h>
#include <stdint.h>

// Read $XDG_RUNTIME_DIR/herdcat/sessions. Missing file or directory is quiet.
void session_store_load(int64_t now_ms, int done_timeout_s);
// Coalesce for one second unless force is set. No runtime dir stops retries.
void session_store_flush(uint64_t generation, int64_t now_ms, bool force);
// -1 when nothing is waiting to be written.
int session_store_timeout(int64_t now_ms);

#endif
