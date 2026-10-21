# Compositor support

The cat and its signs work on any Wayland compositor with the layer-shell
protocol. Jumping to a terminal, the typing desk and noticing which sessions
you have seen also need herdcat to talk to the compositor; this page lists
where that works and how to turn it on.

## Support

| Compositor | Status | Setup | Needs in `PATH` |
| --- | --- | --- | --- |
| niri | Supported | None | `niri` |
| Sway | Supported | None | `swaymsg` |
| Hyprland | Experimental | `compositor_experimental=1` | `hyprctl` |
| Others | Cat and signs only | — | — |

On a supported compositor you get:

- clicking a sign to focus its terminal window,
- the typing desk,
- completions marked as seen when you visit their window,
- typing in a waiting session's terminal showing it as working.

On other compositors the signs still show every session's state, but
completions stay unread until you click the sign or send another prompt, and
there is no typing desk.

Check what herdcat detected with `herdcat --status`: it reports
`compositor=niri`, `Sway`, `Hyprland (experimental)` or `unavailable`, and
`focus-watch=ready` once window tracking works.

## niri

niri is detected automatically through `NIRI_SOCKET`, which niri sets for
programs it starts. Clicking a sign also switches to the window's workspace.

## Sway

Sway is detected automatically through `SWAYSOCK`; no option is needed.
Multiple monitors and XWayland windows have not been tested on Sway yet.

## Hyprland

Hyprland support is experimental and off by default. To try it, add this to
your config and reload:

```ini
compositor_experimental=1
```

Window tracking and focusing have been tested, but full end-to-end testing of
Hyprland is not complete, so expect rough edges. Both the older and the newer
(Lua) Hyprland config syntax are handled. Without the option, herdcat does not
contact Hyprland at all and behaves as on an unsupported compositor.

`compositor_experimental` affects only Hyprland. Hiding the cat when a window
goes fullscreen is separate and works without it.

## When more than one is detected

If several compositor variables are set, for example in a nested session,
herdcat picks niri first, then Hyprland, then Sway. When Hyprland's variable
is set but `compositor_experimental` is 0, herdcat uses no compositor rather
than falling back to Sway.
