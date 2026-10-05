#ifndef INPUT_H
#define INPUT_H

#include "core/herdcat.h"
#include "utils/error.h"

#include <stdatomic.h>
#include <sys/types.h>

// =============================================================================
// INPUT STATE
// =============================================================================

// Renderer-local paw bits received from the private helper socket.
extern atomic_uint *pending_paws;

// =============================================================================
// INPUT MONITORING FUNCTIONS
// =============================================================================

// Start input monitoring with hotplug support - must be checked
HERDCAT_NODISCARD herdcat_error_t
input_start_monitoring(char **paths, int num_paths, char **names, int num_names,
                       int interval, int debug);

// Restart input monitoring with new devices - must be checked
HERDCAT_NODISCARD herdcat_error_t
input_restart_monitoring(char **paths, int num_paths, char **names,
                         int num_names, int interval, int debug);

int input_helper_main(int argc, char **argv);
int input_list_devices(void);
bool input_device_is_keyboard(int fd);
bool input_device_selected(int fd, dev_t identity, char **paths, int num_paths,
                           char **names, int num_names);
void input_process_events(void);
uint32_t input_device_count(void);
// Input nodes the helper's last scan could not open for lack of permission.
uint32_t input_denied_count(void);

// How the user relates to the `input` group: no such group, not a member,
// a member whose processes predate joining (log in again), or held now.
typedef enum {
  INPUT_GROUP_NONE,
  INPUT_GROUP_ABSENT,
  INPUT_GROUP_PENDING,
  INPUT_GROUP_HELD,
} input_group_t;

input_group_t input_group_classify(const char *user, gid_t primary, gid_t group,
                                   char *const *members, const gid_t *held,
                                   int held_count);
input_group_t input_group_state(void);
// One sentence telling the user how to grant keyboard access.
const char *input_access_hint(void);
// The input= word of --status: connected, denied, searching or restarting.
const char *input_status_name(bool alive, uint32_t devices, uint32_t denied);
int64_t input_timestamp(void);

// Cleanup input monitoring resources
void input_cleanup(void);

// Privilege handling for setgid installs, where the binary is setgid to a
// group that may read the keyboard devices (instead of adding the user to the
// `input` group). Both are no-ops when the binary is not setgid.
//
// input_privilege_init() must be the first call in main(): it stops using the
// group (effective gid -> real gid) but keeps it in the saved set, so config
// parsing, Wayland setup and rendering run unprivileged. Rendering calls
// input_privilege_drop() before parsing; the executed helper raises its
// installed group only during device discovery/opening.
//
// input_privilege_drop() gives the group up irrevocably (real, effective and
// saved gid), and exits if that fails rather than run on with it.
void input_privilege_init(void);
void input_privilege_drop(void);

// Get child PID (async-signal-safe accessor for crash handler)
pid_t input_get_child_pid(void);

// Reap child if it exited; true while monitoring process is alive.
bool input_child_is_alive(void);

// Get the helper socket for event-loop polling (-1 if unavailable)
int input_get_wake_fd(void);

#endif  // INPUT_H
