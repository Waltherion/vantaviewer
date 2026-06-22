#include "view_transform.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kMinZoom = 0.02;
constexpr double kMaxZoom = 64.0;

// display-uv -> texture-uv for a clockwise rotation quadrant.
QPointF displayToTexUv(QPointF d, int rot)
{
    switch (rot & 3) {
    case 1:  return QPointF(d.y(), 1.0 - d.x());
    case 2:  return QPointF(1.0 - d.x(), 1.0 - d.y());
    case 3:  return QPointF(1.0 - d.y(), d.x());
    default: return d;
    }
}
// Exact inverse: texture-uv -> display-uv.
QPointF texToDisplayUv(QPointF t, int rot)
{
    switch (rot & 3) {
    case 1:  return QPointF(1.0 - t.y(), t.x());
    case 2:  return QPointF(1.0 - t.x(), 1.0 - t.y());
    case 3:  return QPointF(t.y(), 1.0 - t.x());
    default: return t;
    }
}
} // namespace

void ViewTransform::setRotation(int quadrant)
{
    m_rot = ((quadrant % 4) + 4) % 4;
    clampPan();
}

void ViewTransform::rotateBy(int quadrants)
{
    // Rotation is about the view centre; reset pan so the result stays framed.
    m_pan = QPointF();
    setRotation(m_rot + quadrants);
}

QSizeF ViewTransform::displaySize() const
{
    if (m_rot & 1)
        return QSizeF(m_image.height(), m_image.width());
    return QSizeF(m_image.width(), m_image.height());
}

double ViewTransform::fitScale() const
{
    const QSizeF d = displaySize();
    if (d.width() <= 0 || d.height() <= 0 || m_window.isEmpty())
        return 1.0;
    return std::min(double(m_window.width()) / d.width(),
                    double(m_window.height()) / d.height());
}

void ViewTransform::fit()
{
    m_zoom = 1.0;
    m_pan = QPointF();
}

void ViewTransform::setOneToOne(QPointF anchorPx)
{
    const double fs = fitScale();
    if (fs <= 0)
        return;
    zoomAt(anchorPx, 1.0 / (fs * m_zoom)); // make effectiveScale == 1
}

void ViewTransform::zoomAt(QPointF cursorPx, double factor)
{
    const QPointF imgPt = screenToImage(cursorPx); // texture px under the cursor
    const double next = std::clamp(m_zoom * factor, kMinZoom, kMaxZoom);
    if (next == m_zoom)
        return;
    m_zoom = next;
    // Re-anchor: shift pan so the same texture point lands back under the cursor.
    const QPointF after = imageToScreen(imgPt);
    m_pan += cursorPx - after;
    clampPan();
}

void ViewTransform::panBy(QPointF deltaPx)
{
    m_pan += deltaPx;
    clampPan();
}

void ViewTransform::clampPan()
{
    // Pan limit per axis = |shown - window| / 2. Zoomed in (shown > window) this lets
    // the image pan across while still covering the window; zoomed out (shown < window)
    // it lets the smaller image be nudged around inside the window's margins while
    // staying fully visible. Equal sizes -> no pan.
    const QSizeF d = displaySize();
    const double sc = effectiveScale();
    const double shownW = d.width() * sc;
    const double shownH = d.height() * sc;
    const double maxX = std::abs(shownW - m_window.width()) / 2.0;
    const double maxY = std::abs(shownH - m_window.height()) / 2.0;
    m_pan.setX(std::clamp(m_pan.x(), -maxX, maxX));
    m_pan.setY(std::clamp(m_pan.y(), -maxY, maxY));
}

void ViewTransform::uvScaleOffset(float &usx, float &usy, float &uox, float &uoy) const
{
    const QSizeF d = displaySize();
    const double sc = effectiveScale();
    if (d.width() <= 0 || d.height() <= 0 || m_window.isEmpty() || sc <= 0) {
        usx = usy = 1.0f; uox = uoy = 0.0f;
        return;
    }
    // Over the full window, display-uv spans W/(sc*dispW) horizontally; >1 letterboxes.
    const double sx = double(m_window.width()) / (sc * d.width());
    const double sy = double(m_window.height()) / (sc * d.height());
    usx = float(sx);
    usy = float(sy);
    uox = float(-(m_pan.x() / double(m_window.width())) * sx);
    uoy = float(-(m_pan.y() / double(m_window.height())) * sy);
}

QPointF ViewTransform::screenToImage(QPointF windowPx) const
{
    const QSizeF d = displaySize();
    if (d.width() <= 0 || d.height() <= 0 || m_window.isEmpty())
        return QPointF();
    float usx, usy, uox, uoy;
    uvScaleOffset(usx, usy, uox, uoy);
    const double wux = windowPx.x() / double(m_window.width());
    const double wuy = windowPx.y() / double(m_window.height());
    const QPointF duv((wux - 0.5) * usx + 0.5 + uox,
                      (wuy - 0.5) * usy + 0.5 + uoy);
    const QPointF tuv = displayToTexUv(duv, m_rot);
    return QPointF(tuv.x() * m_image.width(), tuv.y() * m_image.height());
}

QPointF ViewTransform::imageToScreen(QPointF imagePx) const
{
    const QSizeF d = displaySize();
    if (d.width() <= 0 || d.height() <= 0 || m_window.isEmpty() || m_image.isEmpty())
        return QPointF();
    float usx, usy, uox, uoy;
    uvScaleOffset(usx, usy, uox, uoy);
    const QPointF tuv(imagePx.x() / double(m_image.width()),
                      imagePx.y() / double(m_image.height()));
    const QPointF duv = texToDisplayUv(tuv, m_rot);
    const double wux = (duv.x() - 0.5 - uox) / usx + 0.5;
    const double wuy = (duv.y() - 0.5 - uoy) / usy + 0.5;
    return QPointF(wux * m_window.width(), wuy * m_window.height());
}
