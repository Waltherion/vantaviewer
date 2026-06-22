#pragma once

#include <QImage>
#include <QString>

#include "hdr_image.h"

// Builds the info panel (toggled with Ctrl+I): a small rounded translucent card with
// the filename, pixel size, format, HDR kind and colour primaries. Pure rendering to
// an RGBA8 QImage in device pixels -- the window uploads it as an overlay texture.
class InfoOverlay
{
public:
    // monitorHdr drives the "Monitor: HDR/SDR" line; dpr scales for HiDPI sharpness.
    // indexInFolder is 1-based; folderCount <= 0 hides the position line.
    QImage build(const QString &path, const HdrImage &img, bool monitorHdr,
                 int indexInFolder, int folderCount, qreal dpr) const;
};
