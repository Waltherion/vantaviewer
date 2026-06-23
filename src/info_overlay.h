#pragma once

#include <QImage>
#include <QString>
#include <QList>
#include <QPair>

#include "hdr_image.h"
#include "config.h"

// Builds the info panel (toggled with `i`): a small rounded translucent card with
// the filename, pixel size, format, HDR kind and colour primaries. Pure rendering to
// an RGBA8 QImage in device pixels -- the window uploads it as an overlay texture.
class InfoOverlay
{
public:
    // The metadata card (top-left). monitorHdr drives the "Monitor: HDR/SDR" line;
    // indexInFolder is 1-based; folderCount <= 0 hides the position line.
    QImage build(const QString &path, const HdrImage &img, bool monitorHdr,
                 int indexInFolder, int folderCount, float exposureEv,
                 const OverlayStyle &style, qreal dpr) const;

    // The keybinding bar (bottom of screen), laid out in `columns` columns so it
    // stays short. keys is a list of (keyDisplay, label).
    QImage buildKeysBar(const QList<QPair<QString, QString>> &keys,
                        int columns, const OverlayStyle &style, qreal dpr) const;
};
