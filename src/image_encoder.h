#pragma once

#include <QString>
#include <QRect>

#include "hdr_image.h"

// Writes an HdrImage (fp16 linear, BT.709 primaries, 1.0 = 203 nits) to an AVIF,
// applying crop then rotation as pure pixel operations. HDR images are encoded as
// 10-bit PQ; SDR as 8-bit sRGB. Primaries are BT.709 (the decode/display gamut) --
// wide-gamut BT.2020 preservation is a future refinement.
namespace encoder {

struct Result {
    bool ok = false;
    QString message; // error detail, or the written path on success
};

// crop is in image pixels (clamped to the image); an empty/null crop means the whole
// image. rotationQuadrant is clockwise 0..3 (applied after crop). quality is 0..100.
Result encodeAvif(const QString &outPath, const HdrImage &img,
                  const QRect &crop, int rotationQuadrant, int quality = 90);

} // namespace encoder
