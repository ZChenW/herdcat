# Security model

herdcat reads your keyboard and some of your agents' local files, so this page
explains what it can see, what it keeps to itself, and what the installation
changes on your system.

## The keyboard helper

Only one small program, `PREFIX/lib/herdcat/herdcat-input`, gets extra
privilege: it is installed `root:input` with mode `2755` (setgid), so it can
open keyboard devices without you joining the `input` group. The main
`herdcat` program runs as an ordinary process.

The helper:

- opens only keyboard devices under `/dev/input`, read-only, and never grabs
  them or writes to them;
- passes on only which paw to move, when, and how many keyboards it could or
  could not open. Key codes never leave it;
- reads no configuration, writes no files or logs, starts no programs and
  creates no network connections;
- drops its group privilege except while opening a device, and runs under a
  system-call filter that blocks everything else. It runs only on x86-64 and
  AArch64, where that filter is available, and refuses to start otherwise.

`herdcat --status` and `herdcat --list-devices` show whether the helper or the
in-process fallback is in use (see [installing](install.md#the-keyboard-helper)).
Neither logs key codes, even with `enable_debug=1`.

## What other programs can learn

Any user or program on the machine can start the helper and receive the same
paw stream. It
never contains keys, but it does show your typing rhythm and which half of the
keyboard each key was on. That can narrow down what was typed, so typing
activity and timing should not be considered private.

## Threat model

The helper protects raw keyboard events from ordinary programs run by a user
who is **not** in the `input` group and has no device ACL. It does not protect
against:

- root, members of the `input` group, or users with a keyboard ACL;
- other programs running as you, which can already change or inspect herdcat
  itself;
- a compromised kernel, or a memory-safety bug in the helper itself.

Never install the main `herdcat` binary setgid or give it capabilities.

## Installation risks

- `sudo make install` makes only the helper setgid. The `input` group must
  exist, or the root install fails. Installing without root, or with
  `INPUT_HELPER_SETGID=0`, gives an ordinary helper.
- The Arch package sets the helper to `root:input`, mode `2755`. On NixOS the
  module creates the setgid wrapper; Nix store files themselves are never
  setgid. Home Manager, `nix run` and profile installs cannot create the
  wrapper and need device ACLs or the `input` group instead (see
  [the NixOS guide](../nix/NIXOS.md)).
- Filesystems mounted `nosuid` and services started with `NoNewPrivileges=yes`
  silently disable the setgid grant.
- Joining the `input` group, or using the in-process fallback with it, lets
  **every program you run read raw keyboard events**. Prefer the setgid helper
  or narrowly scoped device ACLs.
- A bug in the helper could expose the `input` group or the open keyboards.
  The system-call filter limits what such a bug could do, but cannot rule it
  out.

## What herdcat reads from your agents

All of this stays on your machine. Agent hooks talk to herdcat over a Unix
socket that only your user can use, and remote agents cannot reach it.

- **Hook events**: session IDs, the working directory, the event name and, for
  some agents, the session title or the first line of your first prompt.
  Titles are kept in memory and in a private file under
  `$XDG_RUNTIME_DIR/herdcat/`; they are never logged.
- **Session recordings** (Claude Code and Codex) and **Antigravity's log**:
  with `agent_interrupt_detect=1`, herdcat reads newly added lines to notice
  cancelled and failed turns. Lines are checked in memory and never logged,
  saved or sent. Only files under your home directory, owned by you and not
  reached through symlinks, are opened.
- **Terminal activity**: for quiet detection herdcat reads how many bytes an
  agent process has written (from `/proc`), never what it wrote.
- **Process information**: process names and parent processes to find
  terminals and agents started by other agents, and the `CLAUDE_PID` variable
  of agent processes.
- **The screen of Cursor sessions**, described next.

## Cursor approval screen check

Cursor sends no hook when it asks for approval, so with
`agent_cursor_wait_detect=1` (the default) herdcat **reads the visible screen
of the Cursor session's tmux pane or kitty window** to recognize the approval
menu.

- It reads only while a Cursor tool call has been pending for 1.5 seconds
  without another hook, at most once a second, and only that session's pane or
  window. A tmux session inside kitty is read through tmux, never the outer
  kitty window. Other terminals are never read.
- It asks for the visible screen only, without scrollback, and looks only at
  the last 20 rows above any blank rows at the bottom.
- The text is checked in memory and erased right after. It is never logged,
  saved, sent over herdcat's socket or shown anywhere; only "approval menu or
  not" is kept.
- The check changes what the sign shows; it cannot approve, reject or answer
  anything.
- In kitty it needs the `get-text` remote-control permission, which
  `herdcat setup kitty` does not grant. Adding it lets **any local program that
  can reach kitty's socket read your kitty windows**, not just herdcat, so it is
  your choice; herdcat never adds it.

Set `agent_cursor_wait_detect=0` to turn the check off; it stops immediately on
reload. See [Cursor Agent](agents.md#cursor-agent) for setup and limits.
