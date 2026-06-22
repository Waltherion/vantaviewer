#include "crop_state.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kMinSize = 16.0; // smallest crop side, image px
}

void CropState::begin(const QSize &imageSize)
{
    m_imageSize = imageSize;
    m_rect = QRectF(0, 0, imageSize.width(), imageSize.height());
    m_drag = Handle::None;
    if (m_ratio != Ratio::Free)
        cycleRatio(0); // re-fit the full-image rect to the current ratio
}

void CropState::setImageSize(const QSize &s)
{
    m_imageSize = s;
    clampRect();
}

void CropState::rescale(const QSize &s)
{
    if (m_imageSize.width() > 0 && m_imageSize.height() > 0
        && s.width() > 0 && s.height() > 0) {
        const double sx = double(s.width()) / m_imageSize.width();
        const double sy = double(s.height()) / m_imageSize.height();
        m_rect = QRectF(m_rect.x() * sx, m_rect.y() * sy,
                        m_rect.width() * sx, m_rect.height() * sy);
    }
    m_imageSize = s;
    clampRect();
}

QString CropState::ratioName() const
{
    switch (m_ratio) {
    case Ratio::Original: return QStringLiteral("Original");
    case Ratio::R16_9:    return QStringLiteral("16:9");
    case Ratio::R21_9:    return QStringLiteral("21:9");
    case Ratio::R4_3:     return QStringLiteral("4:3");
    case Ratio::R3_2:     return QStringLiteral("3:2");
    case Ratio::R1_1:     return QStringLiteral("1:1");
    case Ratio::R9_16:    return QStringLiteral("9:16");
    case Ratio::R16_10:   return QStringLiteral("16:10");
    default:              return QStringLiteral("Free");
    }
}

double CropState::ratioWH() const
{
    switch (m_ratio) {
    case Ratio::Original: return m_imageSize.height() > 0
                                  ? double(m_imageSize.width()) / m_imageSize.height() : 0.0;
    case Ratio::R16_9:    return 16.0 / 9.0;
    case Ratio::R21_9:    return 21.0 / 9.0;
    case Ratio::R4_3:     return 4.0 / 3.0;
    case Ratio::R3_2:     return 3.0 / 2.0;
    case Ratio::R1_1:     return 1.0;
    case Ratio::R9_16:    return 9.0 / 16.0;
    case Ratio::R16_10:   return 16.0 / 10.0;
    default:              return 0.0; // free
    }
}

void CropState::cycleRatio(int dir)
{
    constexpr int kCount = 9;
    int v = int(m_ratio) + dir;
    v = ((v % kCount) + kCount) % kCount;
    m_ratio = Ratio(v);

    const double r = ratioWH();
    if (r <= 0.0)
        return; // free: leave the rect as-is

    // Re-fit: largest rect of ratio r centred in the image, clamped to the current rect's
    // centre, fitting inside the image bounds.
    const QPointF c = m_rect.center();
    double w = m_rect.width();
    double h = w / r;
    if (h > m_imageSize.height()) { h = m_imageSize.height(); w = h * r; }
    if (w > m_imageSize.width())  { w = m_imageSize.width();  h = w / r; }
    m_rect = QRectF(c.x() - w / 2.0, c.y() - h / 2.0, w, h);
    clampRect();
}

CropState::Handle CropState::hitTest(const QPointF &p, double tol) const
{
    const QRectF &r = m_rect;
    auto near = [&](double ax, double ay) {
        return std::abs(p.x() - ax) <= tol && std::abs(p.y() - ay) <= tol;
    };
    const bool freeform = (ratioWH() <= 0.0);

    if (near(r.left(),  r.top()))    return Handle::TL;
    if (near(r.right(), r.top()))    return Handle::TR;
    if (near(r.right(), r.bottom())) return Handle::BR;
    if (near(r.left(),  r.bottom())) return Handle::BL;
    if (freeform) {
        if (near(r.center().x(), r.top()))    return Handle::T;
        if (near(r.right(), r.center().y()))  return Handle::R;
        if (near(r.center().x(), r.bottom())) return Handle::B;
        if (near(r.left(),  r.center().y()))  return Handle::L;
    }
    if (r.contains(p))
        return Handle::Inside;
    return Handle::None;
}

void CropState::press(const QPointF &p, double tol)
{
    m_drag = hitTest(p, tol);
    m_dragStartImg = p;
    m_dragStartRect = m_rect;
}

void CropState::release()
{
    m_drag = Handle::None;
    clampRect();
}

void CropState::dragTo(const QPointF &p)
{
    if (m_drag == Handle::None)
        return;

    const double r = ratioWH();
    QRectF rect = m_dragStartRect;

    if (m_drag == Handle::Inside) {
        QPointF d = p - m_dragStartImg;
        rect.translate(d);
        // keep inside image bounds
        if (rect.left() < 0)                       rect.translate(-rect.left(), 0);
        if (rect.top() < 0)                        rect.translate(0, -rect.top());
        if (rect.right() > m_imageSize.width())    rect.translate(m_imageSize.width() - rect.right(), 0);
        if (rect.bottom() > m_imageSize.height())  rect.translate(0, m_imageSize.height() - rect.bottom());
        m_rect = rect;
        return;
    }

    const QPointF cl(std::clamp(p.x(), 0.0, double(m_imageSize.width())),
                     std::clamp(p.y(), 0.0, double(m_imageSize.height())));

    if (r > 0.0) {
        // Locked ratio: corners only. Anchor = opposite corner; size follows the cursor
        // on whichever axis demands the larger rect, then derive the other from r.
        QPointF anchor;
        switch (m_drag) {
        case Handle::TL: anchor = m_dragStartRect.bottomRight(); break;
        case Handle::TR: anchor = m_dragStartRect.bottomLeft();  break;
        case Handle::BR: anchor = m_dragStartRect.topLeft();     break;
        case Handle::BL: anchor = m_dragStartRect.topRight();    break;
        default: return;
        }
        double dw = std::abs(cl.x() - anchor.x());
        double dh = std::abs(cl.y() - anchor.y());
        double w = std::max(dw, dh * r);
        double h = w / r;
        // Don't let it run past the image from the anchor.
        const double maxW = (cl.x() >= anchor.x()) ? (m_imageSize.width() - anchor.x()) : anchor.x();
        const double maxH = (cl.y() >= anchor.y()) ? (m_imageSize.height() - anchor.y()) : anchor.y();
        if (w > maxW) { w = maxW; h = w / r; }
        if (h > maxH) { h = maxH; w = h * r; }
        w = std::max(w, kMinSize); h = std::max(h, kMinSize);
        const double left = (cl.x() >= anchor.x()) ? anchor.x() : anchor.x() - w;
        const double top  = (cl.y() >= anchor.y()) ? anchor.y() : anchor.y() - h;
        m_rect = QRectF(left, top, w, h);
        return;
    }

    // Freeform: move the dragged edge(s) directly.
    double l = rect.left(), t = rect.top(), rg = rect.right(), b = rect.bottom();
    switch (m_drag) {
    case Handle::TL: l = cl.x(); t = cl.y(); break;
    case Handle::TR: rg = cl.x(); t = cl.y(); break;
    case Handle::BR: rg = cl.x(); b = cl.y(); break;
    case Handle::BL: l = cl.x(); b = cl.y(); break;
    case Handle::T:  t = cl.y(); break;
    case Handle::R:  rg = cl.x(); break;
    case Handle::B:  b = cl.y(); break;
    case Handle::L:  l = cl.x(); break;
    default: break;
    }
    if (rg - l < kMinSize) { if (m_drag == Handle::L || m_drag == Handle::TL || m_drag == Handle::BL) l = rg - kMinSize; else rg = l + kMinSize; }
    if (b - t < kMinSize) { if (m_drag == Handle::T || m_drag == Handle::TL || m_drag == Handle::TR) t = b - kMinSize; else b = t + kMinSize; }
    m_rect = QRectF(QPointF(l, t), QPointF(rg, b));
}

void CropState::clampRect()
{
    double l = std::clamp(m_rect.left(), 0.0, double(m_imageSize.width()));
    double t = std::clamp(m_rect.top(), 0.0, double(m_imageSize.height()));
    double rg = std::clamp(m_rect.right(), 0.0, double(m_imageSize.width()));
    double b = std::clamp(m_rect.bottom(), 0.0, double(m_imageSize.height()));
    if (rg - l < kMinSize) rg = std::min(double(m_imageSize.width()), l + kMinSize);
    if (b - t < kMinSize) b = std::min(double(m_imageSize.height()), t + kMinSize);
    m_rect = QRectF(QPointF(l, t), QPointF(rg, b));
}
