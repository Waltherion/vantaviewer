#pragma once

#include <QColor>
#include <QString>

// Shared visual style for the QPainter-rendered overlays (info card, keys bar, crop
// caption, toasts). Pulled from the user config so the look can be themed.
struct OverlayStyle {
    QColor accent{ 150, 190, 255 }; // headers / highlights
    QColor text{ 235, 235, 235 };   // body text
    double cardOpacity = 0.70;      // 0..1 background opacity of the cards
    QString fontFamily;             // empty -> system default
    double fontSize = 11.0;         // overlay base point size
};

// User-tunable appearance + behaviour, from ~/.config/vantaviewer/config.jsonc.
struct Config {
    enum class Background { Black, Transparent, Colour };

    Background background = Background::Black;
    QColor backgroundColour{ 0, 0, 0 }; // used when background == Colour

    OverlayStyle overlay;

    bool infoOverlay = true;  // info card + keys bar shown by default
    int windowWidth = 1280;
    int windowHeight = 800;
    bool fullscreen = false;

    // Load defaults, then merge ~/.config/vantaviewer/config.jsonc (writing a
    // commented template from the bundled default on first run).
    static Config load();

    static QString configPath();
};
