#include "info_overlay.h"

#include <QPainter>
#include <QFileInfo>
#include <QFontMetrics>
#include <QStringList>
#include <QLocale>

QImage InfoOverlay::build(const QString &path, const HdrImage &img, bool monitorHdr,
                          int indexInFolder, int folderCount, qreal dpr) const
{
    const QFileInfo fi(path);
    const QString ext = fi.suffix().toUpper();
    const QString size = QLocale().formattedDataSize(fi.size());

    QStringList lines;
    lines << fi.fileName();
    lines << QStringLiteral("%1 × %2  ·  %3  ·  %4").arg(img.w).arg(img.h).arg(ext).arg(size);
    lines << QStringLiteral("%1  ·  %2")
                 .arg(QString::fromLatin1(hdrKindName(img.kind)))
                 .arg(img.bt2020 ? QStringLiteral("Rec.2020") : QStringLiteral("Rec.709"));
    lines << QStringLiteral("Monitor: %1").arg(monitorHdr ? QStringLiteral("HDR")
                                                          : QStringLiteral("SDR"));
    if (folderCount > 0)
        lines << QStringLiteral("%1 / %2").arg(indexInFolder).arg(folderCount);

    // Lay out in logical pixels, then scale the device-pixel canvas by dpr.
    const int pad = 14;
    const int lineGap = 6;
    QFont font;
    font.setPointSizeF(11.0);
    QFont titleFont = font;
    titleFont.setBold(true);

    QFontMetrics fm(font);
    QFontMetrics fmTitle(titleFont);
    int textW = 0, textH = 0;
    for (int i = 0; i < lines.size(); ++i) {
        const QFontMetrics &m = (i == 0) ? fmTitle : fm;
        textW = std::max(textW, m.horizontalAdvance(lines.at(i)));
        textH += m.height() + (i ? lineGap : 0);
    }
    const int w = textW + pad * 2;
    const int h = textH + pad * 2;

    QImage panel(int(w * dpr), int(h * dpr), QImage::Format_RGBA8888);
    panel.setDevicePixelRatio(dpr);
    panel.fill(Qt::transparent);

    QPainter p(&panel);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    QRectF card(0, 0, w, h);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 175));
    p.drawRoundedRect(card, 10, 10);

    int y = pad;
    for (int i = 0; i < lines.size(); ++i) {
        const QFont &f = (i == 0) ? titleFont : font;
        const QFontMetrics &m = (i == 0) ? fmTitle : fm;
        p.setFont(f);
        p.setPen(i == 0 ? QColor(255, 255, 255) : QColor(210, 210, 210));
        y += m.ascent();
        p.drawText(pad, y, lines.at(i));
        y += m.height() - m.ascent() + lineGap;
    }
    p.end();
    return panel;
}
