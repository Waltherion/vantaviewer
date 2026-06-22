#include "keybindings.h"

#include <QKeyEvent>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>

namespace {

// Base token for a key, layout-tolerant. Returns "" if unmappable.
QString baseToken(int key, const QString &text, bool hasNonShiftMod)
{
    switch (key) {
    case Qt::Key_Left:        return QStringLiteral("left");
    case Qt::Key_Right:       return QStringLiteral("right");
    case Qt::Key_Up:          return QStringLiteral("up");
    case Qt::Key_Down:        return QStringLiteral("down");
    case Qt::Key_Escape:      return QStringLiteral("escape");
    case Qt::Key_Space:       return QStringLiteral("space");
    case Qt::Key_Return:
    case Qt::Key_Enter:       return QStringLiteral("enter");
    case Qt::Key_Tab:         return QStringLiteral("tab");
    case Qt::Key_Backspace:   return QStringLiteral("backspace");
    case Qt::Key_Delete:      return QStringLiteral("delete");
    case Qt::Key_Home:        return QStringLiteral("home");
    case Qt::Key_End:         return QStringLiteral("end");
    case Qt::Key_PageUp:      return QStringLiteral("pageup");
    case Qt::Key_PageDown:    return QStringLiteral("pagedown");
    case Qt::Key_BracketLeft: return QStringLiteral("[");
    case Qt::Key_BracketRight:return QStringLiteral("]");
    case Qt::Key_Equal:       return QStringLiteral("=");
    case Qt::Key_Minus:       return QStringLiteral("-");
    case Qt::Key_Plus:        return QStringLiteral("+");
    case Qt::Key_Comma:       return QStringLiteral(",");
    case Qt::Key_Period:      return QStringLiteral(".");
    default: break;
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12)
        return QStringLiteral("f") + QString::number(key - Qt::Key_F1 + 1);
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return QString(QChar('a' + (key - Qt::Key_A)));
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return QString(QChar('0' + (key - Qt::Key_0)));
    // Non-ASCII / layout-specific (æ, ø, å, ...): use the typed character.
    if (!hasNonShiftMod && text.size() == 1 && text.at(0).isPrint())
        return text.toLower();
    return QString();
}

QString withMods(Qt::KeyboardModifiers mods, const QString &base)
{
    if (base.isEmpty())
        return QString();
    QString s;
    if (mods & Qt::ControlModifier) s += QStringLiteral("ctrl+");
    if (mods & Qt::AltModifier)     s += QStringLiteral("alt+");
    if (mods & Qt::ShiftModifier)   s += QStringLiteral("shift+");
    if (mods & Qt::MetaModifier)    s += QStringLiteral("meta+");
    return s + base;
}

// Parse a config chord string ("Ctrl+I", "Right", "[", "æ") into the same
// normalised form as event tokens.
QString normaliseChord(const QString &raw)
{
    const QStringList parts = raw.trimmed().split(QLatin1Char('+'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return QString();
    Qt::KeyboardModifiers mods;
    QString base;
    for (const QString &pRaw : parts) {
        const QString p = pRaw.trimmed().toLower();
        if (p == QLatin1String("ctrl") || p == QLatin1String("control"))
            mods |= Qt::ControlModifier;
        else if (p == QLatin1String("alt"))
            mods |= Qt::AltModifier;
        else if (p == QLatin1String("shift"))
            mods |= Qt::ShiftModifier;
        else if (p == QLatin1String("meta") || p == QLatin1String("super")
                 || p == QLatin1String("cmd") || p == QLatin1String("win"))
            mods |= Qt::MetaModifier;
        else
            base = p; // last non-modifier token wins
    }
    return withMods(mods, base);
}

// Strip // line comments and /* */ blocks so QJsonDocument can parse JSONC.
QByteArray stripJsonComments(QByteArray in)
{
    static const QRegularExpression line(QStringLiteral("//[^\n]*"));
    static const QRegularExpression block(QStringLiteral("/\\*.*?\\*/"),
                                          QRegularExpression::DotMatchesEverythingOption);
    QString s = QString::fromUtf8(in);
    s.remove(block);
    s.remove(line);
    return s.toUtf8();
}

} // namespace

QString KeyBindings::configPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return dir + QStringLiteral("/vantaviewer/keybindings.jsonc");
}

void KeyBindings::setDefaults()
{
    m_actions.clear();
    auto add = [&](const char *action, std::initializer_list<const char *> chords) {
        QStringList list;
        for (const char *c : chords)
            list << normaliseChord(QString::fromLatin1(c));
        m_actions.insert(QString::fromLatin1(action), list);
    };
    // Defaults follow US-layout key positions; remap freely in the config file.
    add("next",       { "Right", "space" });
    add("prev",       { "Left" });
    add("fit",        { "0", "=" });
    add("oneToOne",   { "1" });
    add("rotateCW",   { "]" });
    add("rotateCCW",  { "[" });
    add("rotate180",  { "r" });
    add("crop",       { "c" });
    add("cropRatio",  { "x" });
    add("cropRatioPrev", { "z" });
    add("save",       { "Ctrl+S" });
    add("info",       { "i" });
    add("fullscreen", { "f" });
    add("quit",       { "q", "Escape" });
}

void KeyBindings::load()
{
    setDefaults();
    const QString path = configPath();
    if (!QFile::exists(path))
        writeTemplate(path);
    else
        mergeFromFile(path);
}

void KeyBindings::mergeFromFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("vantaviewer: cannot read %s", qPrintable(path));
        return;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(stripJsonComments(f.readAll()), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning("vantaviewer: %s is not valid JSONC (%s) -- using defaults",
                 qPrintable(path), qPrintable(err.errorString()));
        return;
    }
    const QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QString action = it.key();
        QStringList chords;
        if (it.value().isString()) {
            chords << normaliseChord(it.value().toString());
        } else if (it.value().isArray()) {
            for (const QJsonValue &v : it.value().toArray())
                if (v.isString())
                    chords << normaliseChord(v.toString());
        }
        chords.removeAll(QString());
        m_actions.insert(action, chords); // user entry replaces the default for that action
    }
}

void KeyBindings::writeTemplate(const QString &path) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("vantaviewer: cannot write %s", qPrintable(path));
        return;
    }
    const QByteArray tmpl = QString::fromUtf8(
        "{\n"
        "  // vantaviewer keybindings. Each action maps to one chord or a list.\n"
        "  // A chord is e.g. \"f\", \"Right\", \"Ctrl+I\", \"[\", or a single key\n"
        "  // like \"æ\" / \"ø\" / \"å\" on a Danish layout.\n"
        "  // Defaults below follow US-layout key positions; remap freely, e.g.\n"
        "  //   \"rotateCCW\": \"æ\",  \"rotateCW\": \"ø\",  \"rotate180\": \"å\"\n"
        "\n"
        "  \"next\":       [\"Right\", \"space\"],\n"
        "  \"prev\":       \"Left\",\n"
        "  \"fit\":        [\"0\", \"=\"],\n"
        "  \"oneToOne\":   \"1\",\n"
        "  \"rotateCW\":   \"]\",\n"
        "  \"rotateCCW\":  \"[\",\n"
        "  \"rotate180\":  \"r\",\n"
        "  \"crop\":       \"c\",\n"
        "  \"cropRatio\":  \"x\",\n"
        "  \"cropRatioPrev\": \"z\",\n"
        "  \"save\":       \"Ctrl+S\",\n"
        "  \"info\":       \"i\",\n"
        "  \"fullscreen\": \"f\",\n"
        "  \"quit\":       [\"q\", \"Escape\"]\n"
        "}\n").toUtf8();
    f.write(tmpl);
    f.commit();
}

QString KeyBindings::actionFor(QKeyEvent *e) const
{
    const bool hasNonShiftMod = e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    const QString base = baseToken(e->key(), e->text(), hasNonShiftMod);
    if (base.isEmpty())
        return QString();
    const QString chord = withMods(e->modifiers(), base);
    for (auto it = m_actions.begin(); it != m_actions.end(); ++it)
        if (it.value().contains(chord))
            return it.key();
    return QString();
}
