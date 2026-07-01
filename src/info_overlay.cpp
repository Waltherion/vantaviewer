#include "info_overlay.h"

#include <QPainter>
#include <QFileInfo>
#include <QFontMetrics>
#include <QStringList>
#include <QLocale>

QImage InfoOverlay::build(const QString &path, const HdrImage &img, bool monitorHdr,
                          int indexInFolder, int folderCount, float exposureEv,
                          const OverlayStyle &style, qreal dpr) const
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
    if (exposureEv != 0.0f)
        lines << QStringLiteral("Exposure: %1%2 EV").arg(exposureEv > 0 ? QStringLiteral("+")
                                                                        : QString())
                                                    .arg(double(exposureEv), 0, 'g', 2);

    // EXIF shot info (only the fields the file actually carries).
    const HdrImage::ExifData &ex = img.exif;
    if (ex.has) {
        if (!ex.camera.isEmpty()) lines << ex.camera;
        if (!ex.lens.isEmpty())   lines << ex.lens;
        QStringList shot;
        if (!ex.exposure.isEmpty()) shot << ex.exposure;
        if (!ex.aperture.isEmpty()) shot << ex.aperture;
        if (!ex.iso.isEmpty())      shot << ex.iso;
        if (!ex.focal.isEmpty())    shot << ex.focal;
        if (!shot.isEmpty())        lines << shot.join(QStringLiteral("  ·  "));
        if (!ex.dateTime.isEmpty()) lines << ex.dateTime;
        if (!ex.gps.isEmpty())      lines << QStringLiteral("GPS: ") + ex.gps;
    }

    if (folderCount > 0)
        lines << QStringLiteral("%1 / %2").arg(indexInFolder).arg(folderCount);

    const int pad = 14;
    const int lineGap = 6;
    QFont font;       font.setPointSizeF(style.fontSize);
    if (!style.fontFamily.isEmpty()) font.setFamily(style.fontFamily);
    QFont titleFont = font; titleFont.setBold(true);
    const QFontMetrics fm(font), fmTitle(titleFont);
    const QColor cardBg(0, 0, 0, int(style.cardOpacity * 255));
    QColor dim = style.text; dim.setAlpha(205);

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
    p.setBrush(cardBg);
    p.drawRoundedRect(QRectF(0, 0, w, h), 10, 10);

    int y = pad;
    for (int i = 0; i < lines.size(); ++i) {
        const QFont &f = (i == 0) ? titleFont : font;
        const QFontMetrics &m = (i == 0) ? fmTitle : fm;
        p.setFont(f);
        p.setPen(i == 0 ? style.text : dim);
        y += m.ascent();
        p.drawText(pad, y, lines.at(i));
        y += m.height() - m.ascent() + lineGap;
    }
    p.end();
    return panel;
}

QImage InfoOverlay::buildKeysBar(const QList<QPair<QString, QString>> &keys,
                                 int columns, const OverlayStyle &style, qreal dpr) const
{
    if (keys.isEmpty() || columns < 1)
        return QImage();

    QFont keyFont;   keyFont.setPointSizeF(style.fontSize - 1.5);
    if (!style.fontFamily.isEmpty()) keyFont.setFamily(style.fontFamily);
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
    p.setBrush(QColor(0, 0, 0, int(style.cardOpacity * 255)));
    p.drawRoundedRect(QRectF(0, 0, w, h), 10, 10);

    QColor keyCol = style.accent;            // the chord, in the accent colour
    QColor labelCol = style.text; labelCol.setAlpha(180);
    p.setFont(keyFont);
    for (int i = 0; i < keys.size(); ++i) {
        const int c = i / rows;
        const int r = i % rows;
        const int baseY = pad + r * rowH + fmKey.ascent();
        p.setPen(keyCol);
        p.drawText(colX[c], baseY, keys.at(i).first);
        p.setPen(labelCol);
        p.drawText(colX[c] + colKeyW[c] + keyGap, baseY, keys.at(i).second);
    }
    p.end();
    return bar;
}
