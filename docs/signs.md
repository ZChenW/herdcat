# Session signs

Each session gets a sign named after its working directory. Claude uses a
rounded rectangle and Codex a circle. Click a plate to jump to its terminal on
niri, including across workspaces; dragging a plate moves the whole cat.
Missing window targets shake briefly. The cat keeps its typing and sleep frames.

A kitty split is focused too when that session's process has both
`KITTY_WINDOW_ID` and `KITTY_LISTEN_ON`. Add these lines to kitty.conf
yourself; this program does not edit that file:

```
allow_remote_control password
remote_control_password "" focus-window
listen_on unix:${XDG_RUNTIME_DIR}/kitty-{kitty_pid}
```

`listen_on` is the socket herdcat talks to. The empty password with one
listed action lets a program on that socket move focus between kitty windows
and nothing else: it cannot type into a window, read one, or open and close
them. This is all the jump needs. The broader `allow_remote_control
socket-only` also works, but then any local program that can open the socket
can type into and read every kitty window. Restart kitty after the edit;
reloading the config does not apply `listen_on`, and only agents started
afterward carry the socket address. Without the socket, a click still focuses
the kitty window and leaves the split alone.

The typing desk follows one session and stops at the first answer. A reported
kitty split selects the session in that split. A click selects that sign until
niri focus leaves its window. Otherwise the newest session in the focused
window is used, including when no split has been reported. Moving to another
split retracts the current desk. The new split's desk appears when you type
there, not merely because focus moved. Unread completions narrow only when a
split report exists: only that split is marked seen, and a report that matches
no session marks none of them. With no report, focusing the window still marks
every session in it seen. Clicking a sign acknowledges that sign and does not
change which other split counts as seen.

A kitty watcher can report which split is focused. Copy
[`integrations/kitty/herdcat_watcher.py`](../integrations/kitty/herdcat_watcher.py) into the kitty config directory and
add one line:

```
watcher herdcat_watcher.py
```

Kitty allows more than one `watcher` line. Keep an existing watcher, such as
`focus_opacity.py`, and do not replace it. Reloading kitty.conf applies this
watcher only to windows and splits opened afterward. The script sends the
same `pane` request on herdcat's control socket. It does not start a
process and does not use `kitten @`, so it needs no extra remote-control
permission. It gives up within a few tens of milliseconds when the cat is
not running, and it writes nothing to the terminal.

## Switch card

Right-click the cat or a sign. The signs step down and the cat holds up a
card with three rows. Nothing on it is labelled; each row shows its choices
and an ink thumb marks the current one.

- **Style**: fan or post. The card closes and the signs come back up in the
  new style.
- **Language**: 中 or EN. Every label changes at once and the card stays.
- **Font**: the family name, drawn in that family, between two arrows. The
  arrows and the scroll wheel step through the families that cover the
  current language. The first entry is the system default.

A paw taps when a row changes. The card closes after 800 ms off the cat and
card, on a second right click, or after 6 seconds idle. Clicks elsewhere on
the screen never reach an overlay, so they cannot close it. There is no card
with `sign_style=off`.

Click the font name to open the **font panel** beside the card. It lists
every family at once, two per row, each drawn in its own face, with a count
and an All / Text / Mono filter. Mono is Fontconfig spacing of 90 or more
(dual, mono, charcell); names are not inspected. The four most recently
chosen families lead the list. More than ten rows scroll with the wheel.

While the panel is open the card steps aside and every session's sign comes
up. The signs take the face under the pointer, so a face is judged on real
signs before it is chosen; moving off the cell puts the chosen face back.
Clicking a cell chooses and saves it and leaves the panel open for another
try. Move the pointer away, or right-click the cat, to finish: the panel and
the card close together. The panel is a separate surface that exists only
while it is open.

Choices live in `${XDG_STATE_HOME:-~/.local/state}/herdcat/prefs`, and the
recent families in `fonts-recent` beside it. The config file is never
rewritten. A choice applies while the config value it replaced is unchanged;
editing that option in the config file afterward makes the file win and
drops the stored choice.

**Fan** (default) raises plates behind the cat. Hover a plate for its name;
waiting plates rise higher, sway and show their names automatically.

![Fan signs, synthetic renderer capture](screenshots/session-signs/fan.png)

**Post** stacks boards on a paper-white outlined pole. Hover the cat to expand
all selected names together. Idle signs appear on hover by default; signs close
150 ms after leaving. Positions follow creation order, with active and recently
updated sessions preferred when the display limit is exceeded.

![Post signs, synthetic renderer capture](screenshots/session-signs/post.png)

These captures use the production renderer and synthetic sessions on a plain
background; they contain no desktop content.

Unread completions stay green with a dot until you visit their window, click
the sign, submit again or end the session. Visiting/clicking starts the normal
completion timer. A completion in the focused window is already seen. When a
kitty split report is available, only that split is seen. Without niri focus
tracking, completions remain unread until clicked or submitted again.

A turn that stops on an error (quota used up, API failure) turns coral with a
cross and follows the same rule: it stays, with a dot, until you have seen it.
In the fan style it is held higher than the others and named on hover.

The fan shows one nameplate at a time: the sign under the pointer, otherwise
the session that has been waiting for approval longest. Answer it and the
next one in line is named.

Typing in an agent's focused terminal moves its sign under the paws as a name
board. It leaves after 2.5 seconds without typing, focus loss, submission or
hiding. Its slot stays reserved and the board passes pointer clicks through.
This requires niri's event stream and uses activity only, never key contents.

| Global option | Values (default first) |
| --- | --- |
| `sign_style` | `fan`, `post`, `off` |
| `sign_max` | `5`; range 1–5 |
| `sign_idle` | `hover`, `always`, `never` |
| `sign_font` | empty = system sans-serif; Fontconfig family, up to 127 bytes |
| `sign_font_size` | `13`; range 10–20, metadata/desk text proportional |
| `sign_animations` | `full`, `reduced` (transitions only), `off` (instant) |
| `sign_language` | `auto`, `en`, `zh` |
| `sign_done` | `sticky`, `timeout` |
| `sign_typing_desk` | `1`, `0` |

All options reload through `-w` or `--reload`. `auto` uses nonempty `LC_MESSAGES`,
then `LANG`: zh locales select simplified Chinese, others English. Use `zh` to
keep the earlier fixed Chinese text. Sizes scale with cat height and output
scale. Larger text leaves less room for names. `never` hides idle signs even
on hover. `timeout` also acknowledges existing unread completions on reload;
returning to `sticky` applies to subsequent completions. `off` restores the
original surface height, cat-only input region and whole-cat agent artwork;
`sign_done` still applies. The temporary `HERDCAT_SIGN_STYLE` override is gone.

FreeType and Fontconfig are required. Only niri supports terminal jumping and
focus tracking. tmux panes stay on the shared window. A kitty click reaches the
matching split only when the socket above is set. Without a split report, kitty
splits still share that window: the desk takes the newest session, and focusing
the window marks every session in it seen. Text supports Latin and CJK with
fallback, but lacks
ligatures, right-to-left shaping and combining marks. At most five of the 32
tracked sessions are displayed. Approval still stays yellow until the tool
finishes; Claude Escape has no hook and retains the existing timeout limitation.
No sign-related periodic wakes remain when there are no sessions, hover or
keys. Reduced/off disable loops, including the desk caret blink; visible working
duration labels still update once a minute.

## Dragging

Hold the left mouse button on the cat and move it horizontally or vertically.
A movement of at least four logical pixels starts a drag; clicking alone keeps
the position. With cursor-shape support the cursor changes to grab/grabbing.
`--doctor` reports whether the protocol is available.

Each output saves its position on release to
`${XDG_STATE_HOME:-$HOME/.local/state}/herdcat/position`. Positions survive
restart, reload and output reconnection, and are clamped when output dimensions
change. `herdcat --reset-position` removes saved positions for every output
and restores `cat_align` / `cat_x_offset` and zero vertical margin. Configuration
files are never rewritten. `overlay_position` chooses the edge from which the
saved vertical margin is measured; `cat_y_offset` still applies inside the bar.

The cat's bounding rectangle intercepts clicks; the rest of the bar remains
click-through. Hidden cats intercept no clicks, including fullscreen hiding
when enabled. Pause still allows dragging. Set `cat_draggable=0` globally or
in a monitor section for complete click-through. Dragging between outputs is
not supported; each output has its own cat and saved position.
