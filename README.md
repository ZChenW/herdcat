# Bongo Cat Wayland Overlay

[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](https://opensource.org/licenses/MIT)
[![Version](https://img.shields.io/badge/version-2.0.2-blue.svg)](https://github.com/saatvik333/wayland-bongocat/releases)

A cute Wayland overlay that shows an animated bongo cat reacting to your keyboard input.

![Demo](assets/demo.gif)

## Features

- 🎯 Real-time keyboard animation
- 🔥 Hot-reload configuration
- 🎮 Auto-hides in fullscreen apps
- 🖥️ Multi-monitor support
- 😴 Idle/scheduled sleep mode
- 🎨 SVG-based rendering (pixel-perfect at any size)
- ⚡ Lightweight (~8MB RAM)

## Quick Start

### Install

```bash
# Arch Linux
yay -S bongocat

# Other distros - build from source
git clone https://github.com/saatvik333/wayland-bongocat.git
cd wayland-bongocat && make
```

### Setup Permissions

```bash
sudo usermod -a -G input $USER
# Log out and back in
```

### Arch package for this fork

The VCS package builds `feature/agent-state` and replaces packages providing
`bongocat`. From the repository root:

```bash
cd packaging/arch
makepkg -si
```

The package installs under `/usr`. If this fork was previously installed with
`sudo make install`, remove those `/usr/local` files after the package installs
successfully so they do not shadow `/usr/bin/bongocat`:

```bash
# From the repository root; preserves user configuration and saved positions.
sudo make PREFIX=/usr/local uninstall
```

Manual installs still default to `/usr/local`; override with `PREFIX` and use
`DESTDIR` for staging. No package script changes input-device permissions.

### Find Your Keyboard

```bash
bongocat-find-devices  # or ./scripts/find_input_devices.sh
```

### Run

```bash
bongocat --watch-config
# Optional: force one monitor from CLI
bongocat --watch-config --monitor eDP-1
```

## Configuration

Create `~/.config/bongocat/bongocat.conf`:

```ini
# ═══════════════════════════════════════════════════════════════════════════
# BONGO CAT CONFIG - Minimal defaults, uncomment to customize
# ═══════════════════════════════════════════════════════════════════════════

# Position & Size
cat_height=110
cat_align=center
# cat_x_offset=0
# cat_y_offset=0

# Appearance
overlay_height=120
overlay_opacity=0
overlay_position=bottom
# mirror_x=0
# mirror_y=0

# Input device (run bongocat-find-devices to find yours)
# Optional: keyboard_device=/dev/input/by-id/YOUR-KEYBOARD-event-kbd

# Multi-monitor (comma-separated monitor names)
# monitor=eDP-1,HDMI-A-1

# Sleep mode (optional)
# idle_sleep_timeout=300
# enable_scheduled_sleep=0
# sleep_begin=22:00
# sleep_end=06:00
```

Every option and command-line flag is listed in
[docs/configuration.md](docs/configuration.md) and in `man bongocat`.

## Session signs and agent status

This fork shows what your AI coding agents are doing. Each session gets a
sign behind the cat: working, waiting for approval, done, or idle. Click a
sign to jump to its terminal on niri. Right-click the cat to switch style,
language and font.

![Fan signs](docs/screenshots/session-signs/fan.png)
![Post signs](docs/screenshots/session-signs/post.png)

Supported agents: Claude Code, Codex, Grok (experimental), Kimi Code, Cursor
Agent, GitHub Copilot CLI, Pi and opencode. Hook examples and bridge scripts
are in [`integrations/`](integrations/).

- [Signs, switch card, font panel and dragging](docs/signs.md)
- [Setting up each agent](docs/agents.md)
- [All options and command-line flags](docs/configuration.md)

## Troubleshooting

<details>
<summary>Permission denied on input device</summary>

```bash
sudo usermod -a -G input $USER
# Then log out and back in
```

</details>

<details>
<summary>Cat not responding to keyboard</summary>

1. Run `bongocat-find-devices` to find correct device
2. Update `keyboard_device` in config
3. Restart bongocat

</details>

<details>
<summary>Not showing on correct monitor</summary>

Set `monitor=YOUR_MONITOR` (single) or `monitor=MON1,MON2` (multi) in config. Find names with `wlr-randr` or `hyprctl monitors`.

</details>

## Building

```bash
git clone https://github.com/saatvik333/wayland-bongocat.git
cd wayland-bongocat
make          # Release build
make debug    # Debug build
```

**Requirements:** wayland-client, FreeType, Fontconfig, pkg-config, gcc/clang, make

## License

MIT License - see [LICENSE](LICENSE)
