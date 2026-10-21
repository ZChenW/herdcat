# Installing herdcat

This page covers installing herdcat, giving it access to your keyboard,
starting it at login, connecting your coding agents and removing it again.

herdcat runs on Wayland compositors that support the layer-shell protocol,
such as niri, Sway and Hyprland. Clicking a sign to jump to its terminal needs
one of the compositors listed in [compositor support](compositors.md).

## Arch Linux

The package in `packaging/arch` builds the latest commit from GitHub:

```bash
git clone https://github.com/ZChenW/herdcat.git
cd herdcat/packaging/arch
makepkg -si
```

The package installs the keyboard helper with the `input` group already set
(see [the keyboard helper](#the-keyboard-helper)), the systemd user service,
shell completions, the man page and the agent integration templates. Python is
an optional dependency, needed only for `herdcat setup`.

## Building from source

Requirements:

- Build: a C compiler (GCC by default), `make` and `pkg-config`
- Libraries: `wayland-client`, FreeType and Fontconfig, with their development headers
- Optional: Python 3 for `herdcat setup`

The generated Wayland protocol files are part of the source tree, so
`wayland-scanner` is not needed for a normal build.

```bash
git clone https://github.com/ZChenW/herdcat.git
cd herdcat
make
sudo make install
```

`make` produces a release build in `build/`; `make debug` builds with debug
symbols and sanitizers instead.

`make install` uses `PREFIX=/usr/local` by default. To install somewhere else,
pass the same `PREFIX` to both steps, because the helper's location is built
into the program:

```bash
make PREFIX=/usr
sudo make install PREFIX=/usr
```

`make install` puts these files under the prefix:

| Path | What it is |
| --- | --- |
| `bin/herdcat` | The overlay and its command-line controls |
| `lib/herdcat/herdcat-input` | The keyboard helper |
| `bin/herdcat-find-devices` | Finds your keyboard |
| `bin/herdcat-setup` | Connects agents (used by `herdcat setup`) |
| `lib/systemd/user/herdcat.service` | The systemd user service |
| `share/herdcat/` | Example configuration and agent integration templates |
| `share/man/man1/herdcat.1` | The man page |
| `share/bash-completion`, `share/zsh`, `share/fish` | Shell completions |

## Nix

A flake is included:

```bash
nix run github:ZChenW/herdcat
```

NixOS and Home Manager modules live in `nix/`; see
[the NixOS guide](../nix/NIXOS.md). The author does not run NixOS, so these
modules get less real-world use than the other install methods. Sign options
that have no module option can be set through `extraConfig`.

## The keyboard helper

The cat moves its paws when you type, so it has to read keyboard events.
herdcat does this with a small separate program, `herdcat-input`, so that you
do not need to join the `input` group.

By default `sudo make install` installs the helper owned by `root:input` with
mode `2755` (setgid). The helper opens the keyboard devices and passes on only
which paw to move and when; it never passes on which key was pressed. The main
`herdcat` program runs without any extra privilege. The
[security model](security.md) explains what the helper can and cannot see.

### Without the setgid helper

To install an ordinary helper without the setgid bit:

```bash
sudo make install INPUT_HELPER_SETGID=0
```

Installing as a normal user (for example into `PREFIX=$HOME/.local`) also
installs an ordinary helper and prints a notice. If the helper is missing or
not executable, herdcat reads the keyboard itself.

In both cases herdcat can only read keyboards your user can already open. Grant
that access in one of two ways:

- **Device ACLs** give your user read access to specific keyboard devices, for
  example `sudo setfacl -m "u:$USER:r" /dev/input/event3`. An ACL set this way
  lasts until the device node is recreated (reboot or replug); a udev rule can
  make it permanent.
- **The `input` group**:

  ```bash
  sudo usermod -a -G input "$USER"
  # Log out and back in
  ```

> [!WARNING]
> Joining the `input` group lets **every program you run read raw keyboard
> events**, including passwords you type. Prefer the setgid helper or narrowly
> scoped device ACLs.

`herdcat --status` and `herdcat --list-devices` show which helper is in use
(`input-helper=standalone` or `input-helper=in-process`) and what access is
missing. See [troubleshooting](troubleshooting.md#permission-denied-on-input-device)
if the cat does not react to typing.

## Finding your keyboard

With no keyboard configured, herdcat uses every keyboard it can open. To pick a
specific one, run:

```bash
herdcat-find-devices      # list keyboards by name
herdcat-find-devices -i   # press keys to find the one you are typing on
```

Then set it in `~/.config/herdcat/herdcat.conf`:

```ini
keyboard_name=YOUR KEYBOARD NAME
# or a device path, preferably a stable alias:
# keyboard_device=/dev/input/by-id/YOUR-KEYBOARD-event-kbd
```

`keyboard_name` also follows the keyboard when it is unplugged and plugged back
in. Add several lines to use several keyboards.

## Running herdcat

```bash
herdcat --watch-config                  # reload when the config file changes
herdcat --watch-config --monitor eDP-1  # force one monitor
```

`herdcat --doctor` checks your configuration, the Wayland protocols your
compositor offers, outputs and keyboard access. See [configuration](configuration.md) for every option and
command-line flag.

## Starting at login

Choose **one** of these methods; using both starts herdcat twice.

- **systemd user service** (installed by `make install` and the Arch package):

  ```bash
  systemctl --user enable --now herdcat
  ```

  The service runs `herdcat -w` as part of your graphical session.

- **niri**: add this line to your niri config:

  ```kdl
  spawn-at-startup "herdcat" "-w"
  ```

## Connecting your agents

herdcat learns about agent sessions through each agent's hooks or plugins.
`herdcat setup` installs them for you; it needs Python 3.

```bash
herdcat setup                    # Detect installed agents, preview, then confirm (default: no)
herdcat setup claude codex       # Select agents
herdcat setup --dry-run          # Preview without writing anything
herdcat setup --status           # Check all twelve integrations
herdcat setup --remove claude    # Remove the integration
herdcat setup claude --yes       # Write without asking (required when not run in a terminal)
```

The agent names are `claude`, `codex`, `grok`, `cursor`, `copilot`, `kimi`,
`pi`, `opencode`, `qwen` and `agy`. `tmux` and `kitty` set up pane focus for
those terminals (see [signs](signs.md#terminals)).

Setup keeps unrelated hooks and settings, backs up every file before changing
it, and updates hooks left by older herdcat or bongocat versions. Restart or
reload open agents afterwards. Codex also needs its `[features] hooks` setting
and a review of the new hooks under `/hooks`.

[Agents](agents.md) describes each agent, what its sign shows, and how to
connect it by hand.

## Uninstalling

1. Remove the agent and terminal integrations while `herdcat-setup` is still
   installed; this restores the original files where nothing else changed them:

   ```bash
   herdcat setup --remove
   ```

2. Stop automatic startup: `systemctl --user disable --now herdcat`, or remove
   the `spawn-at-startup` line from your niri config.

3. Remove the program:

   ```bash
   sudo make uninstall                # from the source tree, with the same PREFIX
   sudo pacman -R herdcat-git         # Arch package
   ```

4. Optionally delete your settings and saved state: `~/.config/herdcat/` (your
   configuration) and `~/.local/state/herdcat/` (saved position, switch-card
   choices, setup backups and receipts).

If you joined the `input` group only for herdcat, remove yourself with
`sudo gpasswd -d "$USER" input` and log in again.
