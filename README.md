# headless-wayland-harness

Drive and screenshot a Wayland GUI application inside a **fully isolated headless
[sway](https://swaywm.org/) compositor**, so an agent (or a CI job) can verify the
UI — click buttons, scroll, type, capture frames — **without touching the host
session**. No workspace switch, no focus stealing, no fighting over the cursor;
you keep using the machine while the app is exercised off-screen.

It works for any native-Wayland app (Slint/winit, GTK, Qt, Electron-on-Wayland, …).

---

## Why this exists (and why the obvious approaches fail)

| Approach | Result |
|---|---|
| Screen-automation tool / `ydotool` on the host | Drives the **shared** cursor and screenshots the **active** output → steals focus, forces workspace switches, and `ydotool`'s kernel-level events leak into your real session. |
| **cage** (kiosk compositor) headless | Renders + `grim`s fine, but its minimal seat **refuses virtual pointers** (`wlr_virtual_pointer_v1 cannot be mapped to an output device`) → zero clicks/scroll. |
| **sway** headless, inject input naively | Screenshots work, but a headless seat has **no input devices**, so it advertises **no pointer/keyboard capability**. Clients (winit/Slint, GTK, …) therefore never bind `wl_pointer`/`wl_keyboard`, and one-shot injected events (`wtype`, `wlrctl`) are **silently dropped**. |

**The fix this harness implements:** keep a **persistent** virtual input device
alive for the sandbox's whole lifetime, so the seat permanently advertises
pointer + keyboard capability and the client binds them. Then every screenshot
and input event lands.

- **Pointer:** `build/vp`, a tiny C program (≈80 lines) that creates one
  `zwlr_virtual_pointer_v1`, holds it open, and executes `move`/`scroll`/`click`
  commands read from a FIFO (opened `O_RDWR` so it never sees EOF).
- **Keyboard:** a held `wtype -s 999999999 -k F15 -k F15` process (huge
  inter-keystroke delay keeps the virtual keyboard device alive; F15 is inert).
- **Screenshots:** `grim`, pointed at the sandbox's own `WAYLAND_DISPLAY`.

`hgui` ties these together and waits until the seat reports capabilities `3`
(pointer + keyboard) before declaring the sandbox ready.

---

## Requirements

Runtime + build dependencies: `gcc`, `pkg-config`, `wayland` (client lib +
`wayland-scanner`), `sway`, `grim`, `wtype`, and `python3` (used to read seat
capabilities). Optional: `wlrctl`.

```bash
# Arch
sudo pacman -S --needed gcc pkgconf wayland sway grim wtype python

# Debian / Ubuntu
sudo apt install gcc pkg-config libwayland-dev wayland-protocols sway grim wtype python3

# Fedora
sudo dnf install gcc pkgconf-pkg-config wayland-devel sway grim wtype python3
```

Your compositor does **not** need to be sway/wlroots — the sandbox is a *nested*
sway you never see. It does need a working `WLR_BACKENDS=headless` (Vulkan or
GL renderer; Mesa/most GPUs are fine).

## Install

```bash
# GitHub
git clone https://github.com/tidynest/headless-wayland-harness.git
# or GitLab mirror
git clone https://gitlab.com/tidynest/headless-wayland-harness.git
cd headless-wayland-harness
./install.sh          # builds build/vp, symlinks `hgui` into ~/.local/bin
```

Re-run `./install.sh` after pulling changes (it rebuilds `vp`).

---

## Usage

```bash
# 1. Launch the app in an isolated 1280x800 sandbox (default size)
hgui start -- myapp --some-flag
hgui start -r 1440x900 -- /path/to/binary      # custom resolution

# 2. Look at it
hgui shot /tmp/frame.png

# 3. Drive it (coordinates are output pixels, origin top-left)
hgui move 640 400        # move pointer
hgui click 1020 26       # move then left-click
hgui click               # click at current position
hgui scroll 200          # wheel down (negative = up)
hgui key Tab             # press a key (Tab, Return, space, Escape, …)
hgui tab 5               # press Tab 5 times
hgui type "hello"        # type into the focused field

# 4. Inspect / tear down
hgui status
hgui stop
```

Typical verify loop: `hgui click X Y` (or `key`) → `hgui shot out.png` → read the
PNG → assert. Find click targets by cropping a `shot` and reading the pixel
coordinates (they map 1:1 to the output, unlike host screen-automation tools).

### Notes & gotchas

- **One sandbox at a time** (fixed state dir `$XDG_RUNTIME_DIR/hgui`). `start`
  tears down any previous instance first.
- **No hot-reload.** Most toolkits don't reload binaries — rebuild the app, then
  `hgui stop && hgui start ...` to pick up changes.
- **Keyboard focus:** a freshly launched app usually has no focused widget. Use
  `hgui tab N` to move focus (watch the focus ring via `shot`), or `hgui click`
  a text field before `hgui type`.
- **Scroll units:** `scroll` values are wheel deltas; ~150–300 per "page" feels
  right for a typical 16px-row grid. Repeat for more.
- **Coordinates must match the resolution** you started with (the pointer's
  absolute-motion extents are set from `-r`).
- Logs live in `$XDG_RUNTIME_DIR/hgui/{sway,vp,kbd}.log`; `hgui status` shows the
  live `WAYLAND_DISPLAY`, seat capabilities, and pids.

---

## Layout

```
headless-wayland-harness/
├── hgui                         CLI dispatcher (symlinked to ~/.local/bin)
├── install.sh                   builds vp, links hgui
├── src/vp.c                     persistent virtual-pointer driver
├── protocol/                    vendored wlr-virtual-pointer protocol XML
├── share/sway-headless.conf.template
└── build/                       generated (gitignored): vp, vp-client.h, vp-code.c
```

## Limitations

- Single instance; no parallel sandboxes (could be added via a named state dir).
- Pointer only does left-click + vertical scroll + absolute move (extend `vp.c`
  for right/middle button, drag, or horizontal scroll).
- Keyboard goes through `wtype` (US keymap); exotic layouts may need care.
- Relies on the wlroots `zwlr_virtual_pointer` + `zwp_virtual_keyboard` protocols
  (sway, wlroots compositors). Not portable to GNOME/mutter as the *sandbox*
  compositor — but the host can be anything.

## Contributing

Issues and pull requests welcome. Good first extensions: right/middle mouse
buttons and drag in `src/vp.c`, a named state dir for parallel sandboxes, or
non-US keymap handling. Keep the dependency footprint small.

Contact: Eric Jingryd <tidynest@proton.me>.

## License

[MIT](LICENSE).

The bundled Wayland protocol definition
`protocol/wlr-virtual-pointer-unstable-v1.xml` is vendored from
[wlroots](https://gitlab.freedesktop.org/wlroots/wlr-protocols) and is also MIT
licensed (Copyright © 2019 Josef Gajdusek); its license header is preserved in
the file.
