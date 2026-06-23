#include "image_ops.h"

#include <QtCore/qfloat16.h>

namespace imageops {

HdrImage cropRotate(const HdrImage &img, const QRect &cropIn, int rot)
{
    HdrImage out;
    if (!img.valid())
        return out;

    QRect full(0, 0, img.w, img.h);
    QRect crop = cropIn;
    if (crop.isNull() || !crop.isValid())
        crop = full;
    crop = crop.intersected(full);
    if (crop.isEmpty())
        crop = full;

    const int cw = crop.width();
    const int ch = crop.height();
    const qfloat16 *src = reinterpret_cast<const qfloat16 *>(img.rgba16f.data());

    rot &= 3;
    out.w = (rot & 1) ? ch : cw;
    out.h = (rot & 1) ? cw : ch;
    out.hdr = img.hdr;
    out.kind = img.kind;
    out.primaries = img.primaries;
    out.rgba16f.resize(size_t(out.w) * out.h * 4);
    qfloat16 *dst = reinterpret_cast<qfloat16 *>(out.rgba16f.data());

    for (int y = 0; y < ch; ++y) {
        const int sy = crop.top() + y;
        for (int x = 0; x < cw; ++x) {
            const int sx = crop.left() + x;
            const size_t s = (size_t(sy) * img.w + sx) * 4;
            int dx, dy;
            switch (rot) {
            case 1: dx = ch - 1 - y; dy = x;          break; // 90 CW
            case 2: dx = cw - 1 - x; dy = ch - 1 - y; break; // 180
            case 3: dx = y;          dy = cw - 1 - x; break; // 270 CW
            default: dx = x;         dy = y;          break;
            }
            const size_t d = (size_t(dy) * out.w + dx) * 4;
            dst[d + 0] = src[s + 0];
            dst[d + 1] = src[s + 1];
            dst[d + 2] = src[s + 2];
            dst[d + 3] = src[s + 3];
        }
    }
    return out;
}

} // namespace imageops
