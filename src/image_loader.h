#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QSet>
#include <memory>

#include "hdr_image.h"

// Decodes images off the GUI thread and caches them by path. The active image is
// decoded at full resolution (for pixel-peeping); neighbours are prefetched at a
// capped resolution so arrow-key navigation is instant. When the user lands on a
// capped neighbour, a full-resolution decode is scheduled and the view upgrades in
// place once it arrives. A small LRU bounds memory (full fp16 frames are large).
class ImageLoader : public QObject
{
    Q_OBJECT
public:
    explicit ImageLoader(QObject *parent = nullptr);

    static constexpr int kPrefetchMaxDim = 3840; // cap for neighbour prefetch

    // Best cached version for a path, or null if nothing is cached yet.
    std::shared_ptr<const HdrImage> cached(const QString &path) const;
    // True if the cached version for path is full resolution.
    bool isFull(const QString &path) const;

    // Decode now, on the calling thread, at full resolution; cache and return it.
    std::shared_ptr<const HdrImage> loadSync(const QString &path);

    // Ensure an async decode is scheduled (full res if full==true, else capped).
    // Emits ready(path) when it completes. No-op if an equal-or-better version is
    // already cached or in flight.
    void request(const QString &path, bool full);

    // Keep these paths (current + neighbours) pinned; evict others beyond capacity.
    void setHot(const QStringList &paths);

signals:
    void ready(const QString &path);

private:
    struct Entry {
        std::shared_ptr<const HdrImage> img;
        bool full = false;
    };
    void insert(const QString &path, std::shared_ptr<const HdrImage> img, bool full);
    void touch(const QString &path);
    void evict();

    QHash<QString, Entry> m_cache;
    QStringList m_lru;            // most-recently-used at front
    QSet<QString> m_hot;          // never evicted
    QSet<QString> m_inFlightFull; // decode scheduled at full res
    QSet<QString> m_inFlightCap;  // decode scheduled at capped res

    static constexpr int kCapacity = 6;
};
