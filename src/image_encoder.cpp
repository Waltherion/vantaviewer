#include "image_encoder.h"

#include <QFile>
#include <QFileInfo>
#include <QBuffer>
#include <QImage>
#include <QColorSpace>
#include <QtCore/qfloat16.h>

#include <avif/avif.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

// Inverse PQ (SMPTE ST 2084) OETF: linear fraction of 10000 cd/m^2 -> encoded [0,1].
float pqInverse(float l)
{
    l = std::clamp(l, 0.0f, 1.0f);
    constexpr float m1 = 0.1593017578125f;
    constexpr float m2 = 78.84375f;
    constexpr float c1 = 0.8359375f;
    constexpr float c2 = 18.8515625f;
    constexpr float c3 = 18.6875f;
    const float lp = std::pow(l, m1);
    return std::pow((c1 + c2 * lp) / (1.0f + c3 * lp), m2);
}

float srgbEncode(float c)
{
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

// Crop (image px) then rotate clockwise by `rot` quadrants. Returns tightly packed
// fp16 RGBA at the final dimensions.
std::vector<uint16_t> cropRotate(const HdrImage &img, QRect crop, int rot,
                                 int &outW, int &outH)
{
    QRect full(0, 0, img.w, img.h);
    if (crop.isNull() || !crop.isValid())
        crop = full;
    crop = crop.intersected(full);
    if (crop.isEmpty())
        crop = full;

    const int cw = crop.width();
    const int ch = crop.height();
    const qfloat16 *src = reinterpret_cast<const qfloat16 *>(img.rgba16f.data());

    rot &= 3;
    outW = (rot & 1) ? ch : cw;
    outH = (rot & 1) ? cw : ch;
    std::vector<uint16_t> out(size_t(outW) * outH * 4);
    qfloat16 *dst = reinterpret_cast<qfloat16 *>(out.data());

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
            const size_t d = (size_t(dy) * outW + dx) * 4;
            dst[d + 0] = src[s + 0];
            dst[d + 1] = src[s + 1];
            dst[d + 2] = src[s + 2];
            dst[d + 3] = src[s + 3];
        }
    }
    return out;
}

uint32_t pngCrc(const uint8_t *buf, size_t len)
{
    uint32_t table[256];
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
        table[n] = c;
    }
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < len; ++i)
        c = table[(c ^ buf[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

// Insert a cICP chunk (primaries, transfer, matrix=0, full-range=1) right after IHDR
// so the decoder reads the PNG's true HDR transfer/primaries.
QByteArray injectCicp(const QByteArray &png, uint8_t primaries, uint8_t transfer)
{
    if (png.size() < 33 || !png.startsWith(QByteArray::fromRawData("\x89PNG\r\n\x1a\n", 8)))
        return png; // not a PNG we recognise; leave untouched

    const uint8_t data[4] = { primaries, transfer, 0, 1 };
    QByteArray chunk;
    auto be32 = [&chunk](uint32_t v) {
        chunk.append(char(v >> 24)); chunk.append(char(v >> 16));
        chunk.append(char(v >> 8));  chunk.append(char(v));
    };
    be32(4);                          // length of data
    QByteArray typeAndData("cICP");
    typeAndData.append(reinterpret_cast<const char *>(data), 4);
    chunk.append(typeAndData);
    be32(pngCrc(reinterpret_cast<const uint8_t *>(typeAndData.constData()),
                size_t(typeAndData.size())));

    // IHDR is the first chunk: 8-byte signature + (4 len + 4 type + 13 data + 4 crc) = 33.
    QByteArray out = png.left(33);
    out.append(chunk);
    out.append(png.mid(33));
    return out;
}

QImage::Format sdr8Format() { return QImage::Format_RGB888; }

// ---- per-format encoders, operating on a pre-cropped/rotated fp16 buffer ----

encoder::Result encodeAvifBuf(const QString &outPath, const qfloat16 *p, int w, int h,
                              bool hdr, int quality)
{
    const int depth = hdr ? 10 : 8;
    const int maxv = (1 << depth) - 1;

    avifImage *image = avifImageCreate(w, h, depth, AVIF_PIXEL_FORMAT_YUV444);
    if (!image)
        return { false, QStringLiteral("avifImageCreate failed") };
    image->colorPrimaries = AVIF_COLOR_PRIMARIES_BT709;
    image->transferCharacteristics = hdr ? AVIF_TRANSFER_CHARACTERISTICS_PQ
                                         : AVIF_TRANSFER_CHARACTERISTICS_SRGB;
    image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT709;
    image->yuvRange = AVIF_RANGE_FULL;

    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, image);
    rgb.format = AVIF_RGB_FORMAT_RGB;
    rgb.depth = depth;
    rgb.ignoreAlpha = AVIF_TRUE;
    if (avifRGBImageAllocatePixels(&rgb) != AVIF_RESULT_OK) {
        avifImageDestroy(image);
        return { false, QStringLiteral("avifRGBImageAllocatePixels failed") };
    }

    for (int y = 0; y < h; ++y) {
        uint8_t *row = rgb.pixels + size_t(y) * rgb.rowBytes;
        for (int x = 0; x < w; ++x) {
            const size_t s = (size_t(y) * w + x) * 4;
            float r = float(p[s + 0]), g = float(p[s + 1]), b = float(p[s + 2]);
            float er, eg, eb;
            if (hdr) {
                er = pqInverse(r * 203.0f / 10000.0f);
                eg = pqInverse(g * 203.0f / 10000.0f);
                eb = pqInverse(b * 203.0f / 10000.0f);
            } else {
                er = srgbEncode(r); eg = srgbEncode(g); eb = srgbEncode(b);
            }
            const int vr = int(std::lround(std::clamp(er, 0.0f, 1.0f) * maxv));
            const int vg = int(std::lround(std::clamp(eg, 0.0f, 1.0f) * maxv));
            const int vb = int(std::lround(std::clamp(eb, 0.0f, 1.0f) * maxv));
            if (depth > 8) {
                uint16_t *d = reinterpret_cast<uint16_t *>(row) + size_t(x) * 3;
                d[0] = uint16_t(vr); d[1] = uint16_t(vg); d[2] = uint16_t(vb);
            } else {
                uint8_t *d = row + size_t(x) * 3;
                d[0] = uint8_t(vr); d[1] = uint8_t(vg); d[2] = uint8_t(vb);
            }
        }
    }

    avifResult r = avifImageRGBToYUV(image, &rgb);
    avifRGBImageFreePixels(&rgb);
    if (r != AVIF_RESULT_OK) {
        avifImageDestroy(image);
        return { false, QString::fromUtf8(avifResultToString(r)) };
    }

    avifEncoder *enc = avifEncoderCreate();
    enc->quality = quality;
    enc->speed = 6;
    avifRWData out = AVIF_DATA_EMPTY;
    r = avifEncoderWrite(enc, image, &out);
    avifEncoderDestroy(enc);
    avifImageDestroy(image);
    if (r != AVIF_RESULT_OK) {
        avifRWDataFree(&out);
        return { false, QString::fromUtf8(avifResultToString(r)) };
    }

    QFile f(outPath);
    if (!f.open(QIODevice::WriteOnly)) {
        avifRWDataFree(&out);
        return { false, QStringLiteral("cannot write %1").arg(outPath) };
    }
    const qint64 sz = qint64(out.size);
    const qint64 written = f.write(reinterpret_cast<const char *>(out.data), sz);
    f.close();
    avifRWDataFree(&out);
    if (written != sz)
        return { false, QStringLiteral("short write to %1").arg(outPath) };
    return { true, outPath };
}

encoder::Result encodePngBuf(const QString &outPath, const qfloat16 *p, int w, int h, bool hdr)
{
    QByteArray bytes;
    if (hdr) {
        // 16-bit PQ, plus a cICP chunk (BT.709 primaries, PQ transfer) injected below.
        QImage img(w, h, QImage::Format_RGBA64);
        for (int y = 0; y < h; ++y) {
            quint16 *row = reinterpret_cast<quint16 *>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const size_t s = (size_t(y) * w + x) * 4;
                const float r = float(p[s + 0]), g = float(p[s + 1]), b = float(p[s + 2]);
                row[x * 4 + 0] = quint16(std::lround(std::clamp(pqInverse(r * 203.0f / 10000.0f), 0.0f, 1.0f) * 65535.0f));
                row[x * 4 + 1] = quint16(std::lround(std::clamp(pqInverse(g * 203.0f / 10000.0f), 0.0f, 1.0f) * 65535.0f));
                row[x * 4 + 2] = quint16(std::lround(std::clamp(pqInverse(b * 203.0f / 10000.0f), 0.0f, 1.0f) * 65535.0f));
                row[x * 4 + 3] = 65535;
            }
        }
        img.setColorSpace(QColorSpace()); // don't embed an sRGB/iCCP profile
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        if (!img.save(&buf, "PNG"))
            return { false, QStringLiteral("PNG encode failed") };
        buf.close();
        bytes = injectCicp(bytes, /*primaries BT.709*/ 1, /*transfer PQ*/ 16);
    } else {
        QImage img(w, h, sdr8Format());
        for (int y = 0; y < h; ++y) {
            uchar *row = img.scanLine(y);
            for (int x = 0; x < w; ++x) {
                const size_t s = (size_t(y) * w + x) * 4;
                row[x * 3 + 0] = uchar(std::lround(srgbEncode(float(p[s + 0])) * 255.0f));
                row[x * 3 + 1] = uchar(std::lround(srgbEncode(float(p[s + 1])) * 255.0f));
                row[x * 3 + 2] = uchar(std::lround(srgbEncode(float(p[s + 2])) * 255.0f));
            }
        }
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        if (!img.save(&buf, "PNG"))
            return { false, QStringLiteral("PNG encode failed") };
    }

    QFile f(outPath);
    if (!f.open(QIODevice::WriteOnly))
        return { false, QStringLiteral("cannot write %1").arg(outPath) };
    f.write(bytes);
    f.close();
    return { true, outPath };
}

encoder::Result encodeQImageSdr(const QString &outPath, const qfloat16 *p, int w, int h)
{
    QImage img(w, h, sdr8Format());
    for (int y = 0; y < h; ++y) {
        uchar *row = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const size_t s = (size_t(y) * w + x) * 4;
            row[x * 3 + 0] = uchar(std::lround(srgbEncode(float(p[s + 0])) * 255.0f));
            row[x * 3 + 1] = uchar(std::lround(srgbEncode(float(p[s + 1])) * 255.0f));
            row[x * 3 + 2] = uchar(std::lround(srgbEncode(float(p[s + 2])) * 255.0f));
        }
    }
    if (!img.save(outPath))
        return { false, QStringLiteral("encode/write failed for %1").arg(outPath) };
    return { true, outPath };
}

} // namespace

namespace encoder {

bool canEncodeExtension(const QString &ext, bool hdr)
{
    const QString e = ext.toLower();
    if (e == QLatin1String("avif"))
        return true;
    if (e == QLatin1String("png"))
        return true;
    if (e == QLatin1String("jpg") || e == QLatin1String("jpeg") || e == QLatin1String("webp")
        || e == QLatin1String("tiff") || e == QLatin1String("tif") || e == QLatin1String("bmp"))
        return !hdr; // these can't carry our HDR
    return false;    // jxl, heic: not yet supported for encoding
}

Result encode(const QString &outPath, const HdrImage &img,
              const QRect &crop, int rotationQuadrant, int quality)
{
    if (!img.valid())
        return { false, QStringLiteral("invalid image") };

    const QString ext = QFileInfo(outPath).suffix().toLower();
    if (!canEncodeExtension(ext, img.hdr)) {
        if (img.hdr && (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg")
                        || ext == QLatin1String("webp") || ext == QLatin1String("tiff")
                        || ext == QLatin1String("tif") || ext == QLatin1String("bmp")))
            return { false, QStringLiteral("%1 can't hold HDR — save as AVIF or PNG").arg(ext.toUpper()) };
        return { false, QStringLiteral("encoding to .%1 is not supported yet").arg(ext) };
    }

    int w = 0, h = 0;
    const std::vector<uint16_t> px = cropRotate(img, crop, rotationQuadrant, w, h);
    const qfloat16 *p = reinterpret_cast<const qfloat16 *>(px.data());

    if (ext == QLatin1String("avif"))
        return encodeAvifBuf(outPath, p, w, h, img.hdr, quality);
    if (ext == QLatin1String("png"))
        return encodePngBuf(outPath, p, w, h, img.hdr);
    return encodeQImageSdr(outPath, p, w, h);
}

} // namespace encoder
