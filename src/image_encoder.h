#pragma once

#include <QString>
#include <QRect>

#include "hdr_image.h"

// Writes an HdrImage (fp16 linear, BT.709 primaries, 1.0 = 203 nits) to a file,
// applying crop then rotation as pure pixel operations. The output format is chosen
// by the output path's extension, preserving the source format where possible:
//   .avif        -> AVIF (HDR: 10-bit PQ; SDR: 8-bit sRGB)
//   .png         -> PNG  (HDR: 16-bit PQ + cICP chunk; SDR: 8-bit sRGB)
//   .jpg .jpeg .webp .tiff .tif .bmp -> via QImage (SDR only)
// HDR primaries are BT.709 (the decode/display gamut); wide-gamut BT.2020 is a future
// refinement. Formats not yet supported for encoding (JXL, HEIC) return an error.
namespace encoder {

struct Result {
    bool ok = false;
    QString message; // error detail, or the written path on success
};

// crop is in image pixels (clamped); an empty/null crop means the whole image.
// rotationQuadrant is clockwise 0..3 (applied after crop). quality is 0..100.
Result encode(const QString &outPath, const HdrImage &img,
              const QRect &crop, int rotationQuadrant, int quality = 92);

// True if the given file extension can be written (so the UI can warn before trying).
bool canEncodeExtension(const QString &ext, bool hdr);

} // namespace encoder
