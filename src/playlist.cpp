#include "playlist.h"

#include <QDir>
#include <algorithm>

static const QStringList kExtensions = {
    // HDR-aware decoders
    QStringLiteral("avif"), QStringLiteral("jxl"),  QStringLiteral("heic"),
    QStringLiteral("heif"),
    // common SDR (Qt + kimageformats plugins)
    QStringLiteral("png"),  QStringLiteral("jpg"),  QStringLiteral("jpeg"),
    QStringLiteral("webp"), QStringLiteral("tiff"), QStringLiteral("tif"),
    QStringLiteral("bmp"),  QStringLiteral("gif"),  QStringLiteral("svg"),
    QStringLiteral("svgz"), QStringLiteral("tga"),  QStringLiteral("ico"),
    QStringLiteral("qoi"),  QStringLiteral("jp2"),  QStringLiteral("ppm"),
    QStringLiteral("pgm"),  QStringLiteral("pbm"),  QStringLiteral("pnm"),
    QStringLiteral("xpm"),  QStringLiteral("xbm"),  QStringLiteral("pcx"),
    QStringLiteral("psd"),
};

void Playlist::load(const QString &dir)
{
    m_dir = dir;
    m_index = 0;
    reload();
}

void Playlist::reload()
{
    const QString keep = current(); // remember the file we're on, to stay on it

    QDir d(m_dir);
    QStringList names;
    for (const QString &n : d.entryList(QDir::Files, QDir::NoSort)) {
        const int dot = n.lastIndexOf(QLatin1Char('.'));
        if (dot >= 0 && kExtensions.contains(n.mid(dot + 1).toLower()))
            names << n;
    }
    std::sort(names.begin(), names.end()); // code-unit order == LC_ALL=C for ASCII

    m_files.clear();
    for (const QString &n : names)
        m_files << d.absoluteFilePath(n);

    if (!keep.isEmpty()) {
        const int i = m_files.indexOf(keep);
        m_index = (i >= 0) ? i : 0;
    } else {
        m_index = 0;
    }
}

QString Playlist::current() const
{
    if (m_files.isEmpty())
        return QString();
    return m_files.at(qBound(0, m_index, m_files.size() - 1));
}

QString Playlist::next()
{
    if (m_files.isEmpty())
        return QString();
    m_index = (m_index + 1) % m_files.size();
    return current();
}

QString Playlist::previous()
{
    if (m_files.isEmpty())
        return QString();
    m_index = (m_index - 1 + m_files.size()) % m_files.size();
    return current();
}

QString Playlist::peekNext() const
{
    if (m_files.isEmpty())
        return QString();
    return m_files.at((m_index + 1) % m_files.size());
}

QString Playlist::peekPrevious() const
{
    if (m_files.isEmpty())
        return QString();
    return m_files.at((m_index - 1 + m_files.size()) % m_files.size());
}

bool Playlist::setCurrentPath(const QString &path)
{
    const int i = m_files.indexOf(path);
    if (i < 0)
        return false;
    m_index = i;
    return true;
}
