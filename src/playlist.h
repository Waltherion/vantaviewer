#pragma once

#include <QString>
#include <QStringList>

// The image files in a directory, sorted case-sensitively by filename (Unicode
// code-unit order == LC_ALL=C byte sort for ASCII), stepped through linearly with
// wrap-around. The viewer loads the directory of the opened file and lands on it;
// the arrow keys walk next/previous. peekNext/peekPrevious don't move, so neighbour
// images can be prefetched without disturbing the current position.
class Playlist
{
public:
    void load(const QString &dir);  // scan dir, position at index 0
    void reload();                  // re-scan, keep position on the same file

    bool isEmpty() const { return m_files.isEmpty(); }
    int size() const { return m_files.size(); }
    int currentIndex() const { return m_files.isEmpty() ? -1 : qBound(0, m_index, m_files.size() - 1); }

    QString current() const;
    QString next();                 // advance, wraps at the end
    QString previous();             // step back, wraps at the start
    QString peekNext() const;       // the next file without moving
    QString peekPrevious() const;   // the previous file without moving

    // Jump to a specific file (absolute path). Returns false if not in the list.
    bool setCurrentPath(const QString &path);

private:
    QString m_dir;
    QStringList m_files; // absolute paths, ascending
    int m_index = 0;
};
