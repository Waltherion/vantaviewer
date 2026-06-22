#pragma once

#include <QRect>

#include "hdr_image.h"

namespace imageops {

// Crop to `crop` (image pixels; a null/empty rect means the whole image) then rotate
// clockwise by `rot` quadrants (0..3). Returns a new HdrImage at the resulting size,
// carrying the source's hdr/kind/bt2020 metadata. Pure pixel shuffling on the fp16
// buffer -- used both to bake an edit for display and to feed the encoder.
HdrImage cropRotate(const HdrImage &img, const QRect &crop, int rot);

} // namespace imageops
