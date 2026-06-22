#include "crop_overlay.h"

#include <QPainter>
#include <QPen>
#include <QFont>
#include <QFontMetrics>

QImage CropOverlay::build(const QSize &windowDev, const QRectF &screenRect,
                          bool freeform, const QString &label, qreal dpr) const
{
    QImage img(windowDev, QImage::Format_RGBA8888);
    img.fill(Qt::transparent);
    if (windowDev.isEmpty())
        return img;

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Dim everything, then punch the crop area back to transparent.
    p.fillRect(QRectF(QPointF(0, 0), QSizeF(windowDev)), QColor(0, 0, 0, 140));
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.fillRect(screenRect, Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    // Rule-of-thirds guides inside the crop.
    QPen thirds(QColor(255, 255, 255, 90));
    thirds.setWidthF(1.0 * dpr);
    p.setPen(thirds);
    for (int i = 1; i <= 2; ++i) {
        const double x = screenRect.left() + screenRect.width() * i / 3.0;
        const double y = screenRect.top() + screenRect.height() * i / 3.0;
        p.drawLine(QPointF(x, screenRect.top()), QPointF(x, screenRect.bottom()));
        p.drawLine(QPointF(screenRect.left(), y), QPointF(screenRect.right(), y));
    }

    // Border.
    QPen border(QColor(255, 255, 255, 230));
    border.setWidthF(1.6 * dpr);
    p.setPen(border);
    p.setBrush(Qt::NoBrush);
    p.drawRect(screenRect);

    // Handles.
    const double hs = 5.0 * dpr; // half-size of a handle square
    QList<QPointF> pts = {
        screenRect.topLeft(), screenRect.topRight(),
        screenRect.bottomRight(), screenRect.bottomLeft()
    };
    if (freeform) {
        pts << QPointF(screenRect.center().x(), screenRect.top())
            << QPointF(screenRect.right(), screenRect.center().y())
            << QPointF(screenRect.center().x(), screenRect.bottom())
            << QPointF(screenRect.left(), screenRect.center().y());
    }
    QPen handleEdge(QColor(0, 0, 0, 180));
    handleEdge.setWidthF(1.0 * dpr);
    p.setPen(handleEdge);
    p.setBrush(QColor(255, 255, 255, 245));
    for (const QPointF &c : pts)
        p.drawRect(QRectF(c.x() - hs, c.y() - hs, hs * 2, hs * 2));

    // Caption (ratio + crop size) just inside the top-left corner of the crop.
    if (!label.isEmpty()) {
        QFont f = p.font();
        f.setPointSizeF(10.0 * dpr);
        p.setFont(f);
        const QFontMetrics fm(f);
        const int tw = fm.horizontalAdvance(label);
        const int th = fm.height();
        const double pad = 5.0 * dpr;
        QRectF box(screenRect.left() + 4 * dpr, screenRect.top() + 4 * dpr,
                   tw + pad * 2, th + pad);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 160));
        p.drawRoundedRect(box, 4 * dpr, 4 * dpr);
        p.setPen(QColor(255, 255, 255, 235));
        p.drawText(box.adjusted(pad, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
    }

    p.end();
    return img;
}
