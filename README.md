# vantaviewer

An **HDR-native image viewer** for Hyprland/Wayland, built on the colour-management
stack of [vantapaper](https://github.com/Waltherion/vantapaper). Ordinary viewers
never tag their Wayland surface, so the compositor assumes SDR — lifting black to
grey on OLED and showing HDR photos wrong. vantaviewer owns its own Vulkan
swapchain and tags the surface via `wp-color-management-v1`, so it looks correct in
**both HDR and SDR monitor modes, with both HDR and SDR images**.

Supported formats: AVIF, JPEG-XL, HEIC/HEIF, UltraHDR gain-map JPEG, 16-bit HDR PNG
(cICP), and all the usual SDR formats (PNG/JPEG/WebP/TIFF/BMP) — same decoders as
vantapaper (`src/hdr_image.cpp`).

## Status — v0.1

Early but functional. What works:

- xdg-toplevel window with its own tagged HDR swapchain — correct in HDR + SDR
  monitor modes, with both HDR and SDR images, true black on OLED.
- Fit / zoom (on cursor) / pan, 90°/180° rotation.
- Folder navigation (←/→) with background neighbour prefetch; live HDR/SDR follow.
- Crop with fixed ratios + freeform, rule-of-thirds guides, info overlay (`i`).
- Export the current view (crop + rotation) to a new AVIF, HDR preserved (`Ctrl+S`).
- Fully remappable keybindings (JSONC).

Planned next: destructive crop with overwrite / save-as, wide-gamut (BT.2020)
export, a file-picker dialog, and live config reload.

### On SDR / non-HDR displays

vantaviewer also works as an ordinary image viewer. If the compositor can't give an
HDR swapchain, it falls back to a correct sRGB pipeline — so it's usable on a laptop
without an HDR screen (HDR images are tone-mapped to SDR, as they are when a monitor
is in SDR mode).

## Usage

```sh
vantaviewer <image>
```

Mouse: scroll to zoom (on the cursor), left-drag to pan, double-click to toggle
fit ↔ 1:1.

Default keys: `→`/`Space` next, `←` previous, `0`/`=` fit, `1` 1:1,
`]`/`[` rotate ±90°, `r` rotate 180°, `c` crop, `x`/`z` cycle crop ratio,
`i` info overlay, `f` fullscreen, `q`/`Esc` quit (`Esc` also cancels crop). Arrow
keys walk the folder of the opened file (sorted), with neighbours prefetched for
instant navigation. HDR/SDR follows the monitor live.

In crop mode, drag the handles to resize and the interior to move; cycle aspect
ratios (Free, Original, 16:9, 21:9, 4:3, 3:2, 1:1, 9:16, 16:10) with `x`/`z` — a
locked ratio shows the four corner handles, freeform shows all eight.

`Ctrl+S` exports the current view (crop + rotation applied) to a new AVIF beside the
original — never overwriting it — named `<name>-crop.avif`. HDR images are written as
10-bit PQ, SDR as 8-bit sRGB, at full resolution and luminance. (The export gamut is
the BT.709 working space; wide-gamut BT.2020 preservation is a planned refinement.)

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
