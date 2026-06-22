#pragma once

#include <QString>
#include <QHash>
#include <QStringList>

class QKeyEvent;

// Maps key chords to named actions, configurable via a JSONC file. Defaults match
// US-layout key positions (e.g. "[" / "]" for rotate); a user on another layout can
// remap them in ~/.config/vantaviewer/keybindings.jsonc.
//
// A chord is written like "f", "Right", "Ctrl+I", "[", or "æ" (single non-ASCII
// keys are matched by their typed character). An action may list several chords:
//   "fit": ["0", "="]
class KeyBindings
{
public:
    // Load built-in defaults, then merge overrides from the config file (creating a
    // commented template on first run if it doesn't exist).
    void load();

    // The action bound to this key event, or an empty string if none.
    QString actionFor(QKeyEvent *e) const;

    // Pretty display of the first chord bound to action ("Ctrl+S", "→", "["); empty
    // if the action has no binding. For the on-screen key list.
    QString primaryChordDisplay(const QString &action) const;

    static QString configPath();

private:
    void setDefaults();
    void mergeFromFile(const QString &path);
    void writeTemplate(const QString &path) const;

    // action -> set of normalised chord strings ("ctrl+i", "[", "right", "å").
    QHash<QString, QStringList> m_actions;
};
