#include "image_encoder.h"
#include "image_ops.h"

#include <QFile>
#include <QFileInfo>
#include <QBuffer>
#include <QImage>
#include <QColorSpace>
#include <QtCore/qfloat16.h>

#include <avif/avif.h>
#include <jxl/encode.h>
#include <libheif/heif.h>

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

// Convert native primaries -> linear BT.709 (for SDR output paths, which are sRGB).
void toBt709(float &r, float &g, float &b, HdrImage::Primaries prim)
{
    float R = r, G = g, B = b;
    if (prim == HdrImage::Primaries::Bt2020) {
        R =  1.660491f * r - 0.587641f * g - 0.072850f * b;
        G = -0.124550f * r + 1.132900f * g - 0.008349f * b;
        B = -0.018151f * r - 0.100579f * g + 1.118730f * b;
    } else if (prim == HdrImage::Primaries::DisplayP3) {
        R =  1.224940f * r - 0.224940f * g;
        G = -0.042057f * r + 1.042057f * g;
        B = -0.019638f * r - 0.078636f * g + 1.098274f * b;
    } else {
        return; // already BT.709
    }
    r = std::max(R, 0.0f); g = std::max(G, 0.0f); b = std::max(B, 0.0f);
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
                              bool hdr, HdrImage::Primaries prim, int quality)
{
    const int depth = hdr ? 10 : 8;
    const int maxv = (1 << depth) - 1;

    avifImage *image = avifImageCreate(w, h, depth, AVIF_PIXEL_FORMAT_YUV444);
    if (!image)
        return { false, QStringLiteral("avifImageCreate failed") };
    switch (prim) {
    case HdrImage::Primaries::Bt2020:
        image->colorPrimaries = AVIF_COLOR_PRIMARIES_BT2020;
        image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT2020_NCL;
        break;
    case HdrImage::Primaries::DisplayP3:
        image->colorPrimaries = AVIF_COLOR_PRIMARIES_SMPTE432;
        image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT709;
        break;
    default:
        image->colorPrimaries = AVIF_COLOR_PRIMARIES_BT709;
        image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT709;
        break;
    }
    image->transferCharacteristics = hdr ? AVIF_TRANSFER_CHARACTERISTICS_PQ
                                         : AVIF_TRANSFER_CHARACTERISTICS_SRGB;
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

encoder::Result encodePngBuf(const QString &outPath, const qfloat16 *p, int w, int h,
                             bool hdr, HdrImage::Primaries prim)
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
        const uint8_t pcicp = prim == HdrImage::Primaries::Bt2020 ? 9
                            : prim == HdrImage::Primaries::DisplayP3 ? 12 : 1;
        bytes = injectCicp(bytes, pcicp, /*transfer PQ*/ 16);
    } else {
        QImage img(w, h, sdr8Format());
        for (int y = 0; y < h; ++y) {
            uchar *row = img.scanLine(y);
            for (int x = 0; x < w; ++x) {
                const size_t s = (size_t(y) * w + x) * 4;
                float r = float(p[s + 0]), g = float(p[s + 1]), b = float(p[s + 2]);
                toBt709(r, g, b, prim);
                row[x * 3 + 0] = uchar(std::lround(srgbEncode(r) * 255.0f));
                row[x * 3 + 1] = uchar(std::lround(srgbEncode(g) * 255.0f));
                row[x * 3 + 2] = uchar(std::lround(srgbEncode(b) * 255.0f));
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

encoder::Result encodeJxlBuf(const QString &outPath, const qfloat16 *p, int w, int h,
                             bool hdr, HdrImage::Primaries prim, int quality)
{
    JxlEncoder *enc = JxlEncoderCreate(nullptr);
    if (!enc)
        return { false, QStringLiteral("JxlEncoderCreate failed") };

    JxlBasicInfo info;
    JxlEncoderInitBasicInfo(&info);
    info.xsize = uint32_t(w);
    info.ysize = uint32_t(h);
    info.num_color_channels = 3;
    info.alpha_bits = 0;
    info.bits_per_sample = hdr ? 16 : 8;
    info.exponent_bits_per_sample = 0;
    info.uses_original_profile = JXL_TRUE; // keep our PQ/sRGB encoding, native primaries
    if (JxlEncoderSetBasicInfo(enc, &info) != JXL_ENC_SUCCESS) {
        JxlEncoderDestroy(enc);
        return { false, QStringLiteral("JxlEncoderSetBasicInfo failed") };
    }

    JxlColorEncoding color = {};
    color.color_space = JXL_COLOR_SPACE_RGB;
    color.white_point = JXL_WHITE_POINT_D65;
    color.primaries = prim == HdrImage::Primaries::Bt2020 ? JXL_PRIMARIES_2100
                    : prim == HdrImage::Primaries::DisplayP3 ? JXL_PRIMARIES_P3
                    : JXL_PRIMARIES_SRGB;
    color.transfer_function = hdr ? JXL_TRANSFER_FUNCTION_PQ : JXL_TRANSFER_FUNCTION_SRGB;
    color.rendering_intent = JXL_RENDERING_INTENT_RELATIVE;
    if (JxlEncoderSetColorEncoding(enc, &color) != JXL_ENC_SUCCESS) {
        JxlEncoderDestroy(enc);
        return { false, QStringLiteral("JxlEncoderSetColorEncoding failed") };
    }

    JxlEncoderFrameSettings *fs = JxlEncoderFrameSettingsCreate(enc, nullptr);
    JxlEncoderSetFrameDistance(fs, JxlEncoderDistanceFromQuality(float(quality)));

    // PQ-encoded uint16 (HDR) or sRGB-encoded uint8 (SDR), 3 interleaved channels,
    // in the image's native primaries (declared above -- no gamut conversion here).
    std::vector<uint8_t> pixels(size_t(w) * h * 3 * (hdr ? 2 : 1));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t s = (size_t(y) * w + x) * 4;
            const float r = float(p[s + 0]), g = float(p[s + 1]), b = float(p[s + 2]);
            if (hdr) {
                uint16_t *d = reinterpret_cast<uint16_t *>(pixels.data()) + (size_t(y) * w + x) * 3;
                d[0] = uint16_t(std::lround(std::clamp(pqInverse(r * 203.0f / 10000.0f), 0.0f, 1.0f) * 65535.0f));
                d[1] = uint16_t(std::lround(std::clamp(pqInverse(g * 203.0f / 10000.0f), 0.0f, 1.0f) * 65535.0f));
                d[2] = uint16_t(std::lround(std::clamp(pqInverse(b * 203.0f / 10000.0f), 0.0f, 1.0f) * 65535.0f));
            } else {
                uint8_t *d = pixels.data() + (size_t(y) * w + x) * 3;
                d[0] = uint8_t(std::lround(srgbEncode(r) * 255.0f));
                d[1] = uint8_t(std::lround(srgbEncode(g) * 255.0f));
                d[2] = uint8_t(std::lround(srgbEncode(b) * 255.0f));
            }
        }
    }
    JxlPixelFormat pf = { 3, hdr ? JXL_TYPE_UINT16 : JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0 };
    if (JxlEncoderAddImageFrame(fs, &pf, pixels.data(), pixels.size()) != JXL_ENC_SUCCESS) {
        JxlEncoderDestroy(enc);
        return { false, QStringLiteral("JxlEncoderAddImageFrame failed") };
    }
    JxlEncoderCloseInput(enc);

    std::vector<uint8_t> out(1 << 20);
    uint8_t *next = out.data();
    size_t avail = out.size();
    JxlEncoderStatus st;
    do {
        st = JxlEncoderProcessOutput(enc, &next, &avail);
        if (st == JXL_ENC_NEED_MORE_OUTPUT) {
            const size_t used = size_t(next - out.data());
            out.resize(out.size() * 2);
            next = out.data() + used;
            avail = out.size() - used;
        }
    } while (st == JXL_ENC_NEED_MORE_OUTPUT);
    const size_t total = size_t(next - out.data());
    JxlEncoderDestroy(enc);
    if (st != JXL_ENC_SUCCESS)
        return { false, QStringLiteral("JXL encode failed") };

    QFile f(outPath);
    if (!f.open(QIODevice::WriteOnly))
        return { false, QStringLiteral("cannot write %1").arg(outPath) };
    f.write(reinterpret_cast<const char *>(out.data()), qint64(total));
    f.close();
    return { true, outPath };
}

encoder::Result encodeHeicBuf(const QString &outPath, const qfloat16 *p, int w, int h,
                              bool hdr, HdrImage::Primaries prim, int quality)
{
    heif_context *ctx = heif_context_alloc();
    heif_encoder *encoder = nullptr;
    if (heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &encoder).code != heif_error_Ok
        || !encoder) {
        heif_context_free(ctx);
        return { false, QStringLiteral("no HEVC encoder available") };
    }
    heif_encoder_set_lossy_quality(encoder, quality);

    const int depth = hdr ? 10 : 8;
    const int maxv = (1 << depth) - 1;
    heif_image *image = nullptr;
    const heif_chroma chroma = hdr ? heif_chroma_interleaved_RRGGBB_LE
                                   : heif_chroma_interleaved_RGB;
    if (heif_image_create(w, h, heif_colorspace_RGB, chroma, &image).code != heif_error_Ok) {
        heif_encoder_release(encoder); heif_context_free(ctx);
        return { false, QStringLiteral("heif_image_create failed") };
    }
    heif_image_add_plane(image, heif_channel_interleaved, w, h, depth);
    int stride = 0;
    uint8_t *plane = heif_image_get_plane(image, heif_channel_interleaved, &stride);

    for (int y = 0; y < h; ++y) {
        uint8_t *row = plane + size_t(y) * stride;
        for (int x = 0; x < w; ++x) {
            const size_t s = (size_t(y) * w + x) * 4;
            const float r = float(p[s + 0]), g = float(p[s + 1]), b = float(p[s + 2]);
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
            if (hdr) {
                uint16_t *d = reinterpret_cast<uint16_t *>(row) + size_t(x) * 3;
                d[0] = uint16_t(vr); d[1] = uint16_t(vg); d[2] = uint16_t(vb);
            } else {
                uint8_t *d = row + size_t(x) * 3;
                d[0] = uint8_t(vr); d[1] = uint8_t(vg); d[2] = uint8_t(vb);
            }
        }
    }

    heif_color_profile_nclx *nclx = heif_nclx_color_profile_alloc();
    nclx->color_primaries =
        prim == HdrImage::Primaries::Bt2020 ? heif_color_primaries_ITU_R_BT_2020_2_and_2100_0
        : prim == HdrImage::Primaries::DisplayP3 ? heif_color_primaries_SMPTE_EG_432_1
        : heif_color_primaries_ITU_R_BT_709_5;
    nclx->transfer_characteristics = hdr ? heif_transfer_characteristic_ITU_R_BT_2100_0_PQ
                                         : heif_transfer_characteristic_IEC_61966_2_1;
    nclx->matrix_coefficients = prim == HdrImage::Primaries::Bt2020
        ? heif_matrix_coefficients_ITU_R_BT_2020_2_non_constant_luminance
        : heif_matrix_coefficients_ITU_R_BT_709_5;
    nclx->full_range_flag = 1;
    heif_image_set_nclx_color_profile(image, nclx);

    heif_encoding_options *opts = heif_encoding_options_alloc();
    opts->output_nclx_profile = nclx;
    heif_image_handle *handle = nullptr;
    heif_error enc = heif_context_encode_image(ctx, image, encoder, opts, &handle);
    heif_encoding_options_free(opts);
    heif_nclx_color_profile_free(nclx);
    heif_image_release(image);
    heif_encoder_release(encoder);
    if (enc.code != heif_error_Ok) {
        if (handle) heif_image_handle_release(handle);
        heif_context_free(ctx);
        return { false, QString::fromUtf8(enc.message) };
    }

    const heif_error wr = heif_context_write_to_file(ctx, outPath.toLocal8Bit().constData());
    if (handle) heif_image_handle_release(handle);
    heif_context_free(ctx);
    if (wr.code != heif_error_Ok)
        return { false, QString::fromUtf8(wr.message) };
    return { true, outPath };
}

encoder::Result encodeQImageSdr(const QString &outPath, const qfloat16 *p, int w, int h,
                                HdrImage::Primaries prim)
{
    QImage img(w, h, sdr8Format());
    for (int y = 0; y < h; ++y) {
        uchar *row = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const size_t s = (size_t(y) * w + x) * 4;
            float r = float(p[s + 0]), g = float(p[s + 1]), b = float(p[s + 2]);
            toBt709(r, g, b, prim);
            row[x * 3 + 0] = uchar(std::lround(srgbEncode(r) * 255.0f));
            row[x * 3 + 1] = uchar(std::lround(srgbEncode(g) * 255.0f));
            row[x * 3 + 2] = uchar(std::lround(srgbEncode(b) * 255.0f));
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
    if (e == QLatin1String("avif") || e == QLatin1String("png") || e == QLatin1String("jxl")
        || e == QLatin1String("heic") || e == QLatin1String("heif"))
        return true; // carry HDR + any primaries
    if (e == QLatin1String("jpg") || e == QLatin1String("jpeg") || e == QLatin1String("webp")
        || e == QLatin1String("tiff") || e == QLatin1String("tif") || e == QLatin1String("bmp"))
        return !hdr; // these can't carry our HDR
    return false;
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
            return { false, QStringLiteral("%1 can't hold HDR — save as AVIF, PNG or JXL").arg(ext.toUpper()) };
        return { false, QStringLiteral("encoding to .%1 is not supported yet").arg(ext) };
    }

    const HdrImage edited = imageops::cropRotate(img, crop, rotationQuadrant);
    if (!edited.valid())
        return { false, QStringLiteral("crop/rotate produced an empty image") };
    const int w = edited.w, h = edited.h;
    const qfloat16 *p = reinterpret_cast<const qfloat16 *>(edited.rgba16f.data());

    if (ext == QLatin1String("avif"))
        return encodeAvifBuf(outPath, p, w, h, img.hdr, img.primaries, quality);
    if (ext == QLatin1String("png"))
        return encodePngBuf(outPath, p, w, h, img.hdr, img.primaries);
    if (ext == QLatin1String("jxl"))
        return encodeJxlBuf(outPath, p, w, h, img.hdr, img.primaries, quality);
    if (ext == QLatin1String("heic") || ext == QLatin1String("heif"))
        return encodeHeicBuf(outPath, p, w, h, img.hdr, img.primaries, quality);
    return encodeQImageSdr(outPath, p, w, h, img.primaries);
}

} // namespace encoder
