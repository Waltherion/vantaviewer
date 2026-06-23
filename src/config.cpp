#include "config.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

QByteArray stripJsonComments(const QByteArray &in)
{
    static const QRegularExpression line(QStringLiteral("//[^\n]*"));
    static const QRegularExpression block(QStringLiteral("/\\*.*?\\*/"),
                                          QRegularExpression::DotMatchesEverythingOption);
    QString s = QString::fromUtf8(in);
    s.remove(block);
    s.remove(line);
    return s.toUtf8();
}

QColor parseColour(const QString &s, const QColor &fallback)
{
    const QColor c(s.trimmed());
    return c.isValid() ? c : fallback;
}

} // namespace

QString Config::configPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return dir + QStringLiteral("/vantaviewer/config.jsonc");
}

Config Config::load()
{
    Config cfg;

    const QString path = configPath();
    if (!QFile::exists(path)) {
        // First run: drop the bundled, commented default next to the keybindings.
        QFile res(QStringLiteral(":/config.default.jsonc"));
        if (res.open(QIODevice::ReadOnly)) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QSaveFile f(path);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(res.readAll());
                f.commit();
            }
        }
        return cfg; // defaults
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("vantaviewer: cannot read %s", qPrintable(path));
        return cfg;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(stripJsonComments(f.readAll()), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning("vantaviewer: %s is not valid JSONC (%s) -- using defaults",
                 qPrintable(path), qPrintable(err.errorString()));
        return cfg;
    }
    const QJsonObject o = doc.object();

    if (o.contains(QStringLiteral("background"))) {
        const QString b = o.value(QStringLiteral("background")).toString().trimmed().toLower();
        if (b == QLatin1String("transparent")) {
            cfg.background = Config::Background::Transparent;
        } else if (b == QLatin1String("black") || b.isEmpty()) {
            cfg.background = Config::Background::Black;
        } else {
            const QColor c(b);
            if (c.isValid()) {
                cfg.background = Config::Background::Colour;
                cfg.backgroundColour = c;
            }
        }
    }

    cfg.overlay.accent = parseColour(o.value(QStringLiteral("accent")).toString(), cfg.overlay.accent);
    cfg.overlay.text   = parseColour(o.value(QStringLiteral("text")).toString(), cfg.overlay.text);
    if (o.contains(QStringLiteral("overlayOpacity")))
        cfg.overlay.cardOpacity = qBound(0.0, o.value(QStringLiteral("overlayOpacity")).toDouble(0.70), 1.0);
    const QString font = o.value(QStringLiteral("font")).toString();
    if (!font.isEmpty())
        cfg.overlay.fontFamily = font;
    if (o.contains(QStringLiteral("fontSize")))
        cfg.overlay.fontSize = qBound(6.0, o.value(QStringLiteral("fontSize")).toDouble(11.0), 48.0);

    if (o.contains(QStringLiteral("infoOverlay")))
        cfg.infoOverlay = o.value(QStringLiteral("infoOverlay")).toBool(true);
    if (o.contains(QStringLiteral("windowWidth")))
        cfg.windowWidth = qMax(160, o.value(QStringLiteral("windowWidth")).toInt(1280));
    if (o.contains(QStringLiteral("windowHeight")))
        cfg.windowHeight = qMax(120, o.value(QStringLiteral("windowHeight")).toInt(800));
    if (o.contains(QStringLiteral("fullscreen")))
        cfg.fullscreen = o.value(QStringLiteral("fullscreen")).toBool(false);

    return cfg;
}
