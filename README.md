# vantaviewer

An **HDR-native image viewer** for Hyprland/Wayland, built on the colour-management
stack of [vantapaper](https://github.com/Waltherion/vantapaper). HDR-capable image
viewers are rare on Linux — a lightweight one built directly for Hyprland's colour
management more so.

In HDR mode Hyprland deliberately raises the SDR black floor (it looks better on
non-OLED panels), and untagged content goes through that lifted SDR path — so on
OLED, blacks come up grey and HDR photos render wrong unless you correct it
yourself. vantaviewer owns its own Vulkan swapchain and tags it via
`wp-color-management-v1`, so its content is colour-managed directly: true 0-nit
black on OLED and correct rendering in **both HDR and SDR monitor modes, with both
HDR and SDR images** — without touching your compositor config.

Supported formats: AVIF, JPEG-XL, HEIC/HEIF, UltraHDR gain-map JPEG, 16-bit HDR PNG
(cICP), and all the usual SDR formats (PNG/JPEG/WebP/TIFF/BMP) — same decoders as
vantapaper (`src/hdr_image.cpp`).

## Status — v0.2

Early but functional. What works:

- xdg-toplevel window with its own tagged HDR swapchain — correct in HDR + SDR
  monitor modes, with both HDR and SDR images, true black on OLED.
- Fit / zoom (on cursor) / pan, 90°/180° rotation.
- Folder navigation (←/→) with background neighbour prefetch; live HDR/SDR follow.
- Crop with fixed ratios + freeform, rule-of-thirds guides; default-on info overlay
  (`i`) that also lists the keybindings.
- Save the current view (crop + rotation) preserving the source format — overwrite
  in place (`Ctrl+S`) or save as (`Ctrl+Shift+S`).
- Fully remappable keybindings (JSONC).

Planned next: wide-gamut (BT.2020) export, JPEG-XL/HEIC encoding, a file-picker
dialog, and live config reload.

### On SDR / non-HDR displays

vantaviewer also works as an ordinary image viewer. If the compositor can't give an
HDR swapchain, it falls back to a correct sRGB pipeline — so it's usable on a laptop
without an HDR screen (HDR images are tone-mapped to SDR, as they are when a monitor
is in SDR mode).

## Usage

```sh
vantaviewer path/to/image.avif      # or .jxl .heic .jpg .png .webp …
```

Opens the file and lets the arrow keys page through the rest of its folder.

Mouse: scroll to zoom (on the cursor), left-drag to pan, double-click to toggle
fit ↔ 1:1.

Default keys: `→`/`Space` next, `←` previous, `0`/`=` fit, `1` 1:1,
`]`/`[` rotate ±90°, `r` rotate 180°, `c` crop, `x`/`z` cycle crop ratio,
`Ctrl+S` save (overwrite), `Ctrl+Shift+S` save as, `i` toggle the info overlay,
`f` fullscreen, `q`/`Esc` quit (`Esc` also cancels crop / a save prompt). Arrow
keys walk the folder of the opened file (sorted), with neighbours prefetched for
instant navigation. HDR/SDR follows the monitor live. The info overlay is shown by
default and lists the current keybindings.

In crop mode, drag the handles to resize and the interior to move; cycle aspect
ratios (Free, Original, 16:9, 21:9, 4:3, 3:2, 1:1, 9:16, 16:10) with `x`/`z` — a
locked ratio shows the four corner handles, freeform shows all eight.

Saving applies the current crop + rotation and **preserves the source format**,
chosen by the output file's extension:

- **AVIF** — HDR as 10-bit PQ, SDR as 8-bit sRGB.
- **PNG** — HDR as 16-bit PQ with a `cICP` chunk (round-trips losslessly), SDR as 8-bit.
- **JPEG / WebP / TIFF / BMP** — SDR only (these can't carry our HDR).
- JPEG-XL and HEIC encoding aren't supported yet (you'll get a clear message).

`Ctrl+S` overwrites the current file in place (a confirm prompt appears; Enter
accepts). `Ctrl+Shift+S` is *Save as* — type a target path; the format follows the
extension you type. Everything is written at full resolution and luminance. (The
working/export gamut is BT.709; wide-gamut BT.2020 preservation is a planned
refinement.)

All keys are remappable in a JSONC config at
`~/.config/vantaviewer/keybindings.jsonc` (written with defaults on first run) —
e.g. on a Danish layout you can bind rotation to `æ`/`ø`/`å`.

If Qt logs go to journald instead of the terminal, run with
`QT_FORCE_STDERR_LOGGING=1`.

## Build & install

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/vantaviewer <image>
```

Install system-wide (binary, `vantaviewer.desktop`, and the hicolor app icon):

```sh
sudo cmake --install build
```

On Arch, the bundled `PKGBUILD` clones and builds from git:

```sh
makepkg -si
```

Requires Qt6 (Gui/GuiPrivate/ShaderTools/Concurrent), Vulkan, wayland-protocols,
libavif, libjxl, libheif, libultrahdr, lcms2. **Vulkan RHI is required for HDR on
Wayland.** Build needs `qt6-shadertools`, `cmake`, `ninja`, `wayland-protocols`.

To register it as the default image handler:

```sh
xdg-mime default vantaviewer.desktop image/png image/jpeg image/avif image/jxl \
  image/heic image/heif image/webp image/tiff image/bmp
```

## Credits

Icon and concept by Walther (GitHub: [@Waltherion](https://github.com/Waltherion)).
Built on the colour-management work in
[vantapaper](https://github.com/Waltherion/vantapaper).

## License

GPL-3.0-or-later.
