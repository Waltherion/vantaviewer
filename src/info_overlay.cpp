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
                 .arg(QString::fromLatin1(primariesName(img.primaries)));
    lines << QStringLiteral("Monitor: %1").arg(monitorHdr ? QStringLiteral("HDR")
                                                          : QStringLiteral("SDR"));
    if (folderCount > 0)
        lines << QStringLiteral("%1 / %2").arg(indexInFolder).arg(folderCount);

    const int pad = 14;
    const int lineGap = 6;
    QFont font;       font.setPointSizeF(11.0);
    QFont titleFont = font; titleFont.setBold(true);
    const QFontMetrics fm(font), fmTitle(titleFont);

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
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 180));
    p.drawRoundedRect(QRectF(0, 0, w, h), 10, 10);

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

QImage InfoOverlay::buildKeysBar(const QList<QPair<QString, QString>> &keys,
                                 int columns, qreal dpr) const
{
    if (keys.isEmpty() || columns < 1)
        return QImage();

    QFont keyFont;   keyFont.setPointSizeF(9.5);
    const QFontMetrics fmKey(keyFont);
    const int keyGap = 10;   // between a key chord and its label
    const int colGap = 26;   // between columns
    const int pad = 12;
    const int rowH = fmKey.height() + 3;

    const int rows = (keys.size() + columns - 1) / columns;

    // Per-column key/label widths (column-major fill).
    QVector<int> colKeyW(columns, 0), colLabelW(columns, 0);
    for (int i = 0; i < keys.size(); ++i) {
        const int c = i / rows;
        colKeyW[c] = std::max(colKeyW[c], fmKey.horizontalAdvance(keys.at(i).first));
        colLabelW[c] = std::max(colLabelW[c], fmKey.horizontalAdvance(keys.at(i).second));
    }
    QVector<int> colX(columns, 0);
    int x = pad;
    for (int c = 0; c < columns; ++c) {
        colX[c] = x;
        x += colKeyW[c] + keyGap + colLabelW[c] + (c + 1 < columns ? colGap : 0);
    }
    const int w = x + pad;
    const int h = rows * rowH + pad * 2;

    QImage bar(int(w * dpr), int(h * dpr), QImage::Format_RGBA8888);
    bar.setDevicePixelRatio(dpr);
    bar.fill(Qt::transparent);

    QPainter p(&bar);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 175));
    p.drawRoundedRect(QRectF(0, 0, w, h), 10, 10);

    p.setFont(keyFont);
    for (int i = 0; i < keys.size(); ++i) {
        const int c = i / rows;
        const int r = i % rows;
        const int baseY = pad + r * rowH + fmKey.ascent();
        p.setPen(QColor(245, 245, 245));
        p.drawText(colX[c], baseY, keys.at(i).first);
        p.setPen(QColor(170, 170, 170));
        p.drawText(colX[c] + colKeyW[c] + keyGap, baseY, keys.at(i).second);
    }
    p.end();
    return bar;
}
