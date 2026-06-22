#pragma once

#include <QImage>
#include <QRectF>
#include <QSize>

// Renders the crop chrome -- dimmed surround, border, rule-of-thirds guides and
// handles -- into a window-sized RGBA8 image (device pixels) that the window blits
// as a single overlay quad. Everything is in device px; line/handle sizes scale by
// dpr for crispness.
class CropOverlay
{
public:
    // screenRect is the crop rectangle mapped into window/device pixels. freeform
    // adds the four edge-midpoint handles to the four corners. label (e.g.
    // "16:9 · 1920×1080") is drawn as a small caption by the crop.
    QImage build(const QSize &windowDev, const QRectF &screenRect,
                 bool freeform, const QString &label, qreal dpr) const;
};
