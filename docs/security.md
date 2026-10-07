# Keyboard input security

The renderer is unprivileged. Only `PREFIX/lib/herdcat/herdcat-input` is
installed `root:input`, mode 2755. This executable has one source/link unit,
`src/input/input_helper.c`, and only libc as a dynamic dependency. It contains
no configuration parser, renderer, log-file handling or control socket code.
Missing/nonexecutable helpers use the existing `/proc/self/exe --input-helper`
fallback, requiring input-group membership or device ACLs. An executable
helper that rejects its arguments or fails hardening is not bypassed by a
fallback. `--status` reports the running selection; `--list-devices` reports
selection and renderer-visible devices, then helper-readable keyboard counts.

## Threat model

Protect raw keyboard events from ordinary processes of a user who is **not**
in the input group and has no keyboard ACL. Any user able to execute the helper
can request its coarse paw/timing stream; this is not an authorization boundary
between desktop applications. Same-UID code can also modify the renderer and
read its paw state. Root, input-group members, device ACL holders, a compromised
kernel, and a memory-safety compromise of the privileged helper are outside
this boundary. Do not install the renderer setgid or grant it capabilities.

## Eight enforced requirements

1. The first operation in `main` is `clearenv`, checked for failure. No helper
   code reads environment variables, config, sysfs, current-directory files
   or logs. The renderer spawns it with an empty environment. A dynamic loader
   necessarily runs before `main`; libc's secure-execution handling applies
   to real setgid execution. Environment clearing cannot undo loader activity
   in an ordinary unprivileged invocation.
2. Arguments are exactly `fd interval filtered count [event-path ...]`.
   Decimal digits only: fd 3..INT_MAX, interval 0..3600, filtered 0..1, count
   0..32; the argument count must agree. Auto mode requires count zero; an
   empty explicit selection opens nothing. `fstat` and `fcntl` reject closed,
   read-only, regular-file, terminal/character-device and anonymous descriptors.
   Output supports a write-only pipe (never a self-reading O_RDWR FIFO) or an AF_UNIX SOCK_SEQPACKET socket with
   same-UID parent peer credentials. Eventfd is deliberately rejected because
   its eight-byte writes cannot represent the existing 24-byte packet.
3. Only canonical `/dev/input/event` plus decimal digits is accepted, with
   a bounded path length. Traversal, repeated separators, aliases, suffixes,
   signs and relative paths are rejected. The helper opens `/dev/input` as a
   directory with O_NOFOLLOW, then opens the final component with
   O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK. `fstat` requires a character device
   with major 13. Automatic discovery reads only `/dev/input` and applies the
   same checks. At most 32 devices remain open, deduplicated by device identity.
   The unprivileged renderer resolves configured aliases and reads sysfs
   names, then supplies canonical paths. It refreshes selectors on scan status
   packets and restarts the helper only when the resolved set changes.
4. `setresgid` lowers the effective gid before validation and immediately after
   every attempted device open, including failures; `getresgid` verifies all
   three fields. With hotplug enabled, the saved input gid is retained and
   restored only around directory/device opens, because permanent dropping
   would prevent reconnects. Scan-once mode permanently sets real/effective/
   saved gids to the real gid after the initial scan. Existing supplementary
   groups belong to the invoking user; this program cannot revoke that user's
   independently granted input-group access.
5. There are no system/popen/exec calls, file writes, logging or stderr output.
   Standard streams and unrelated inherited fds are closed. The only write
   target is the validated output fd. Packets contain paw bits, aggregate
   device/denial counts, monotonic timestamps and explicitly zero reserved
   padding. Raw key codes and device event records never leave the helper.
6. PR_SET_DUMPABLE=0 and PR_SET_NO_NEW_PRIVS=1 are mandatory. Dumpability is
   reset after every gid transition. A seccomp BPF allowlist is mandatory on
   x86-64/AArch64; unsupported architectures or failed installation reject
   startup. It permits read, output-fd-only write, poll/ppoll, close, fstat/
   newfstatat, clock_gettime and exit. Hotplug additionally needs getdents64,
   exact read-only openat flags, setresgid/getresgid, and PR_SET_DUMPABLE=0.
   Libc directory allocation needs brk, non-executable mmap and munmap, plus
   F_GETFL; signal delivery needs rt_sigreturn. The only allowed ioctl request
   is EVIOCGBIT(EV_KEY) with the fixed capability-buffer size. No exec, socket
   creation, device grabs, filesystem mutation or device writes is permitted.
7. Devices are opened read-only and never grabbed. EVIOCGRAB is absent from
   the implementation and blocked by seccomp; no device-write syscall exists.
8. Poll checks output HUP/ERR/NVAL even when idle or no keyboards are present.
   Failed writes terminate the helper; scan status heartbeats also expose a
   lost pipe reader. The design does not rely on PR_SET_PDEATHSIG across setgid
   execution. A peer that retains a duplicated output endpoint can keep it
   alive; endpoint loss, rather than the parent's PID alone, is the lifetime
   contract, and the renderer does not pass its endpoint to other processes.

## Installation and residual risks

`sudo make install` installs only the helper with setgid. The input group must
exist; root installs fail if ownership cannot be established. Non-root installs
use 0755 and print a notice. `INPUT_HELPER_SETGID=0` disables the privilege grant.
Arch's package sets root:input/2755 in `package()`. Nix store files remain 0755;
the NixOS module uses `security.wrappers`. Home Manager/profile installs need
the system wrapper, ACLs or the legacy group grant. `nosuid` mounts and inherited
no_new_privs can suppress setgid; metadata alone does not prove effective access.

Other processes cannot obtain raw key contents through this protocol, but can
observe typing rhythm and which keyboard half activated a paw. These signals
can reduce uncertainty about typed input. Access and timing are not private.
Any vulnerability in the small helper could expose the saved group or open
keyboard fds; seccomp narrows consequences but does not prove memory safety.
Keyboard names/aliases rely on Linux sysfs availability. Reject unsupported or
oversized explicit selections instead of guessing or reading other devices.

## Audit and acceptance

Read `src/input/input_helper.c` and `include/platform/input_protocol.h` in full;
check every open, ioctl, gid transition, packet field and seccomp branch.
`readelf -d build/herdcat-input` should list only libc; `nm -D` should show no
exec/system/popen imports. Check the helper's build rule, installed ownership,
permissions, trusted parent directories and mount/service restrictions.

Run `make format-check`, `make release`, `make test`, `make lint`. The new
validator tests substitute device metadata and gid transitions, and exercise
real seccomp rejection in child processes. The real helper's pipe tests verify
environment handling, packet shape, real unprivileged gids, no_new_privs,
seccomp and endpoint loss. They do not establish a real setgid grant.

Outside a restricted sandbox, install the helper and use a user outside input:

```sh
make input-helper-runtime-build INPUT_HELPER_PATH=/usr/local/lib/herdcat/herdcat-input
python3 scripts/test_input_helper_runtime.py --expect-keyboard
# Optional: type during the observation window to verify the paw stream.
python3 scripts/test_input_helper_runtime.py --expect-keyboard --expect-paw
```

Also test live unplug/replug and configured name/alias selection, verify
`herdcat --status` through a running renderer, compare `--list-devices` with and
without the helper, and evaluate/build the NixOS wrapper configuration. The
runtime script creates sockets but never creates setgid files or writes devices.
See the [implementation report](performance/input-helper-report.md) for actual
results and gaps in this checkout.
