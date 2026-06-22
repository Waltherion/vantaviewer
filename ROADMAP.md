# Roadmap

VantaViewer's identity is **HDR-first colour-managed viewing** — that's the niche and
the part to keep deepening. This is a wishlist of directions, not commitments. Many
items are benchmarked against [swayimg](https://github.com/artemsen/swayimg) (a mature
lightweight SDR viewer); the goal is to *selectively* adopt what fits, not to clone it
1:1. Rough effort: 🟢 small · 🟡 medium · 🔴 large.

## Colour / HDR (the core niche — go deeper)
- 🟡 Wide-gamut **BT.2020 export** — preserve the source gamut instead of the BT.709
  working space (needs a native-primaries decode path).
- 🟡 **JPEG-XL and HEIC encoding** — so those formats can be overwritten / saved in place.
- 🟢 **Configurable SDR white level** — match the compositor's `sdrbrightness` vs the
  203-nit reference (one shader uniform).
- 🔴 **Dolphin/KDE HDR thumbnailer** — a `thumbnail` subcommand (tone-map HDR→SDR) +
  a KF6 ThumbnailCreator plugin, so file-manager previews aren't grey.

## Viewing
- 🟡 **More formats** — SVG (Qt SVG), GIF, TGA/PNM/QOI, **OpenEXR** (HDR!), farbfeld;
  maybe RAW (libraw). SDR ones are mostly free via QImage.
- 🔴 **Animation playback** — GIF, animated WebP, APNG (multi-frame decode + loop).
- 🟢 **Transparency checkerboard** background for images with alpha (toggle).
- 🟡 **Scale modes** (fit / fill / real / width / height) + sampling/antialiasing options.

## Navigation / browsing
- 🔴 **Gallery / thumbnail grid mode** — browse a folder visually.
- 🟢 **Slideshow** — timed auto-advance.
- 🟢 **Sort options** — name / mtime / size / random.
- 🟡 **Recursive directories**, multiple file arguments, and **stdin/pipe** input.

## Editing
- 🟢 **Flip / mirror** (horizontal + vertical) alongside 90/180 rotate.
- 🟡 **Lossless JPEG rotate** (jpegtran-style) for JPEG sources.
- 🟡 **File-picker dialog** for Save-as (alternative to the in-place text field).

## Metadata
- 🟡 **EXIF display** (camera / exposure / date / GPS) in the info overlay.

## Config / extensibility
- 🟢 **Live config reload** — re-read keybindings without restarting.
- 🟡 **Exec arbitrary commands** on a keypress (delete / copy / run a script).
- 🟡 **Theming in config** — overlay colours, fonts, which info fields to show.

## Packaging
- 🟢 **AUR package** once the AUR malware-lockdown lifts.
