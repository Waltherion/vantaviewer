#include "image_loader.h"

#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>

ImageLoader::ImageLoader(QObject *parent) : QObject(parent) {}

std::shared_ptr<const HdrImage> ImageLoader::cached(const QString &path) const
{
    auto it = m_cache.constFind(path);
    return it != m_cache.constEnd() ? it->img : nullptr;
}

bool ImageLoader::isFull(const QString &path) const
{
    auto it = m_cache.constFind(path);
    return it != m_cache.constEnd() && it->full;
}

std::shared_ptr<const HdrImage> ImageLoader::loadSync(const QString &path)
{
    if (auto it = m_cache.constFind(path); it != m_cache.constEnd() && it->full) {
        touch(path);
        return it->img;
    }
    auto img = std::make_shared<HdrImage>(decodeImage(path, 0));
    if (!img->valid())
        return nullptr;
    insert(path, img, /*full=*/true);
    return img;
}

void ImageLoader::request(const QString &path, bool full)
{
    if (path.isEmpty())
        return;
    // Already have an equal-or-better version?
    if (auto it = m_cache.constFind(path); it != m_cache.constEnd() && (it->full || !full))
        return;
    // Already scheduled at an equal-or-better level?
    if (m_inFlightFull.contains(path) || (!full && m_inFlightCap.contains(path)))
        return;

    if (full) m_inFlightFull.insert(path);
    else      m_inFlightCap.insert(path);

    const int maxDim = full ? 0 : kPrefetchMaxDim;
    auto *watcher = new QFutureWatcher<HdrImage>(this);
    connect(watcher, &QFutureWatcher<HdrImage>::finished, this, [this, watcher, path, full]() {
        HdrImage decoded = watcher->result();
        watcher->deleteLater();
        if (full) m_inFlightFull.remove(path);
        else      m_inFlightCap.remove(path);
        if (decoded.valid()) {
            // Don't let a late capped result clobber a full one already cached.
            auto it = m_cache.constFind(path);
            if (!(it != m_cache.constEnd() && it->full && !full))
                insert(path, std::make_shared<HdrImage>(std::move(decoded)), full);
            emit ready(path);
        }
    });
    watcher->setFuture(QtConcurrent::run([path, maxDim]() {
        return decodeImage(path, maxDim);
    }));
}

void ImageLoader::setHot(const QStringList &paths)
{
    m_hot = QSet<QString>(paths.constBegin(), paths.constEnd());
    evict();
}

void ImageLoader::insert(const QString &path, std::shared_ptr<const HdrImage> img, bool full)
{
    m_cache.insert(path, Entry{ std::move(img), full });
    touch(path);
    evict();
}

void ImageLoader::touch(const QString &path)
{
    m_lru.removeAll(path);
    m_lru.prepend(path);
}

void ImageLoader::evict()
{
    // Drop least-recently-used entries beyond capacity, never the hot set.
    for (int i = m_lru.size() - 1; i >= 0 && m_cache.size() > kCapacity; --i) {
        const QString &p = m_lru.at(i);
        if (m_hot.contains(p))
            continue;
        m_cache.remove(p);
        m_lru.removeAt(i);
    }
}
