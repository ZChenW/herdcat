# Troubleshooting

Fixes for the most common problems: keyboard access, a cat that does not react
to typing, missing signs, and clicks that do not focus a terminal. Start with
`herdcat --doctor`, which checks configuration, outputs and keyboard access in
one go.

## Permission denied on input device

`herdcat --status` shows `input=denied`, and `herdcat --list-devices` says
which fix applies.

Both commands also show which keyboard helper is in use:

- `input-helper=standalone`: herdcat uses the installed `herdcat-input`
  helper. Check that `PREFIX/lib/herdcat/herdcat-input` (for example
  `/usr/local/lib/herdcat/herdcat-input`) is owned by `root:input` with mode
  `2755`. A filesystem mounted `nosuid`, or a service started with
  `NoNewPrivileges=yes`, silently drops the setgid grant. Reinstall with root
  privileges (`sudo make install`); only the helper should be setgid, never the
  `herdcat` binary itself.
- `input-helper=in-process`: the helper is missing or not executable, so
  herdcat reads the keyboard itself and needs device ACLs or the `input` group
  (see [installing](install.md#without-the-setgid-helper)).

Joining the `input` group only takes effect after a new login. If
`getent group input` lists you but `id` does not, restart herdcat from a shell
that already has the group:

```bash
systemctl --user stop herdcat
newgrp input
herdcat --watch-config
```

## Cat not responding to keyboard

1. Run `herdcat-find-devices -i` and press a few keys to find the right device.
2. Set `keyboard_name` (or `keyboard_device`) in `~/.config/herdcat/herdcat.conf`.
3. Restart herdcat, or run `herdcat --reload`.

If `herdcat --status` shows `input=searching`, no matching keyboard exists
yet; check the name for typos. A keyboard selected by name is picked up again
when it is plugged back in.

## No sign for an agent

1. Check that the agent's hooks are installed: `herdcat setup --status`. See
   [agents](agents.md) for each agent's setup.
2. Restart or reload the agent; most agents only load hooks at startup. Codex
   also needs the new hooks reviewed and trusted under `/hooks`.
3. Run `herdcat --sessions` to see which sessions the cat knows about.
4. Send the agent one message; some agents only report once a turn starts.

Idle sessions are shown only while the pointer is over the cat, unless you set
`sign_idle=always`. At most `sign_max` sessions (default 10) get a sign.

## Clicking a sign does not focus the terminal

Jumping to a window works on niri and Sway without any setup. Hyprland needs
`compositor_experimental=1` and is experimental. Other compositors cannot
focus windows. See [compositor support](compositors.md).

Jumping to a split or pane inside the terminal works in kitty, tmux and
WezTerm; kitty and tmux need `herdcat setup kitty` or `herdcat setup tmux`
first. Ghostty only focuses the window. See [terminals](signs.md#terminals)
for setup and limits.

A sign that shakes briefly means herdcat could not find the session's window,
for example a tmux session with no attached client.
