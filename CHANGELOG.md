# Changelog

## v0.3 — 2026-06-23

- **Wide-gamut (BT.2020) export.** Decoders now keep the source's native primaries
  instead of converting to BT.709 at decode time; the shader does the BT.2020→BT.709
  conversion for display (unchanged on screen), and the encoder writes true BT.2020
  (AVIF nclx / PNG cICP) when the source was BT.2020. Saving an HDR image no longer
  clips it to the BT.709 working gamut — verified round-trip-preserved (PNG bit-exact).
- **More input formats.** Added GIF, SVG, TGA, ICO, QOI, JP2, PNM (ppm/pgm/pbm),
  XPM/XBM, PCX and PSD (decoded via Qt + kimageformats), on top of the existing
  AVIF/JXL/HEIC/PNG/JPEG/WebP/TIFF/BMP. `.desktop` MIME types updated to match.
- **True-HDR float formats.** OpenEXR, Radiance `.hdr` and PFM now decode as real HDR
  (scene-linear float taken directly, not SDR-clamped) — verified preserving linear
  values incl. highlights above 1.0.

## v0.2 — 2026-06-22

- Crop is now an edit: press Enter ("⏎ apply") to bake it into the working image so
  the cropped-away area disappears in-app; crop mode stays armed for fast successive
  crops. Navigating away with an unsaved crop/rotation prompts "Save changes?"
  (Enter = save, N = discard, Esc = cancel). Saving refreshes the in-memory image
  and the cache so paging back shows the edited result.
- Info overlay is shown by default and now lists the active keybindings; toggle off
  with `i`.
- Save preserves the source format (chosen by the output extension):
  - AVIF (HDR 10-bit PQ / SDR 8-bit), PNG (HDR 16-bit PQ + cICP / SDR 8-bit), and
    SDR JPEG/WebP/TIFF/BMP via QImage. HDR PNG round-trips losslessly.
  - JPEG-XL and HEIC encoding are not supported yet (clear error shown).
- `Ctrl+S` now **overwrites** the current file in place (with a confirm prompt,
  default-yes on Enter), preserving its format.
- `Ctrl+Shift+S` is **Save as** — a minimal in-place text field to type a target
  path; the format follows the extension you type.
- Graceful sRGB fallback gated on the actual swapchain, so HDR shading is never used
  on an SDR display.

## v0.1 — 2026-06-22

First public release. An HDR-native image viewer for Hyprland/Wayland.

- Own QRhi/Vulkan swapchain tagged via `wp-color-management-v1` (windows-scRGB in
  HDR, sRGB in SDR) — true black on OLED, correct in both monitor modes with both
  HDR and SDR images. Graceful sRGB fallback on non-HDR displays.
- Decodes AVIF, JPEG-XL, HEIC/HEIF, UltraHDR gain-map JPEG, 16-bit HDR PNG (cICP),
  and SDR PNG/JPEG/WebP/TIFF/BMP. Active image decoded at full resolution.
- Fit / cursor-anchored zoom / pan, 90°/180° rotation.
- Folder navigation (←/→) with background neighbour prefetch and in-place full-res
  upgrade; live per-window HDR/SDR polling of Hyprland.
- Crop overlay: fixed ratios (16:9, 21:9, 4:3, 3:2, 1:1, 9:16, 16:10) + Original +
  freeform, rule-of-thirds guides, corner/edge handles.
- Info overlay (`i`): filename, dimensions, format, size, HDR kind, primaries,
  monitor mode, position in folder.
- Export (`Ctrl+S`) the current view (crop + rotation) to a new AVIF beside the
  original — HDR as 10-bit PQ, SDR as 8-bit sRGB, full resolution. Never overwrites.
- Remappable keybindings via `~/.config/vantaviewer/keybindings.jsonc`.

Known limitations / planned: destructive crop with overwrite + save-as; wide-gamut
(BT.2020) export; file-picker dialog; live config reload.
