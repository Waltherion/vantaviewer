#include "viewer_window.h"
#include "cm_tagging.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QScreen>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QCursor>
#include <QPainter>
#include <QFontMetrics>
#include <QExposeEvent>
#include <QPlatformSurfaceEvent>
#include <QVulkanInstance>
#include <QGuiApplication>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <rhi/qshader.h>

#include "image_encoder.h"

#include <algorithm>
#include <cmath>

static QShader loadShader(const QString &name)
{
    QFile f(name);
    if (f.open(QIODevice::ReadOnly))
        return QShader::fromSerialized(f.readAll());
    qWarning("vantaviewer: failed to load shader %s", qPrintable(name));
    return QShader();
}

ViewerWindow::ViewerWindow(QVulkanInstance *inst, bool hdrMode)
    : m_inst(inst), m_hdrMode(hdrMode)
{
    setSurfaceType(QSurface::VulkanSurface);
    setVulkanInstance(inst);
    setTitle(QStringLiteral("vantaviewer"));
    resize(1280, 800);
    m_keys.load();

    // A neighbour/full-res decode finished: if it's the current image, upgrade in
    // place (keep zoom/pan/rotation); otherwise it just sits warm in the cache.
    connect(&m_loader, &ImageLoader::ready, this, [this](const QString &path) {
        if (path == m_currentPath) {
            if (auto img = m_loader.cached(path))
                setImage(img, /*resetView=*/false);
        }
    });

    // Live HDR/SDR follow: re-tag + re-render when the monitor's preset flips.
    connect(&m_hdrMon, &HdrMonitor::hdrModeChanged, this, &ViewerWindow::setHdrMode);
    connect(this, &QWindow::screenChanged, this, [this](QScreen *s) {
        if (s) m_hdrMon.setScreenName(s->name());
    });

    m_toastTimer.setSingleShot(true);
    connect(&m_toastTimer, &QTimer::timeout, this, [this]() {
        m_toastVisible = false;
        requestUpdate();
    });
}

ViewerWindow::~ViewerWindow() = default;

bool ViewerWindow::openPath(const QString &path)
{
    const QFileInfo fi(path);
    m_playlist.load(fi.absolutePath());
    m_playlist.setCurrentPath(fi.absoluteFilePath());
    const bool ok = showPath(fi.absoluteFilePath());

    if (QScreen *s = screen())
        m_hdrMon.setScreenName(s->name());
    m_hdrMon.start();
    return ok;
}

bool ViewerWindow::showPath(const QString &path)
{
    if (path.isEmpty())
        return false;
    m_currentPath = path;

    // Show the best we have immediately; if nothing is cached, decode synchronously
    // (the very first image, and any cold neighbour) so something appears at once.
    std::shared_ptr<const HdrImage> img = m_loader.cached(path);
    if (!img)
        img = m_loader.loadSync(path);
    if (img)
        setImage(img, /*resetView=*/true);

    m_loader.request(path, /*full=*/true);                 // upgrade if only capped
    m_loader.request(m_playlist.peekNext(), /*full=*/false);
    m_loader.request(m_playlist.peekPrevious(), /*full=*/false);
    updateHotSet();
    return img != nullptr;
}

void ViewerWindow::navigate(int dir)
{
    if (m_playlist.isEmpty())
        return;
    const QString path = dir >= 0 ? m_playlist.next() : m_playlist.previous();
    showPath(path);
}

void ViewerWindow::updateHotSet()
{
    m_loader.setHot({ m_currentPath, m_playlist.peekNext(), m_playlist.peekPrevious() });
}

void ViewerWindow::setHdrMode(bool hdr)
{
    if (hdr == m_hdrMode && m_colorApplied)
        return;
    m_hdrMode = hdr;

    // Re-tag the surface (HDR scRGB vs SDR sRGB); the shader's SDR branch follows
    // the same flag. The new image description applies on the next committed frame.
    if (m_color && m_color->valid()) {
        if (m_hdrMode)
            m_color->setWindowsScrgb();
        else
            m_color->setSrgb();
    }
    if (m_infoVisible)
        rebuildInfoPanel(); // the "Monitor: HDR/SDR" line changes
    requestUpdate();
}

void ViewerWindow::setImage(std::shared_ptr<const HdrImage> image, bool resetView)
{
    if (!image || !image->valid())
        return;
    m_image = std::move(image);
    m_incomingDirty = true;
    m_view.setImageSize(QSize(m_image->w, m_image->h));
    const QSize newSize(m_image->w, m_image->h);
    if (resetView) {
        m_view.setRotation(0);
        m_view.fit();
        if (m_crop.active())
            m_crop.begin(newSize); // new picture -> reset crop to the full image
    } else {
        // In-place upgrade -- keep zoom/pan/rotation; zoom=1 stays fit, pan is in
        // window px, so the higher-res image looks identical and just gets sharper.
        if (m_crop.active())
            m_crop.rescale(newSize); // scale the crop rect to the higher-res image
    }

    if (m_infoVisible)
        rebuildInfoPanel(); // dimensions/kind may have changed

    if (!m_initialized)
        return; // uploaded once the surface exists (init/render)

    resizeTexture(*m_image);
    requestUpdate();
}

void ViewerWindow::resizeTexture(const HdrImage &img)
{
    const QSize want(img.w, img.h);
    if (m_tex && m_tex->pixelSize() != want) {
        m_tex->destroy();
        m_tex->setPixelSize(want);
        m_tex->create();
    }
}

void ViewerWindow::init()
{
    QRhiVulkanInitParams params;
    params.inst = m_inst;
    params.window = this;

    m_rhi.reset(QRhi::create(QRhi::Vulkan, &params));
    if (!m_rhi)
        qFatal("vantaviewer: failed to create QRhi (Vulkan backend)");

    m_sc.reset(m_rhi->newSwapChain());
    m_sc->setWindow(this);

    const QByteArray want = qgetenv("VANTAVIEWER_FORMAT").toLower();
    auto trySet = [&](QRhiSwapChain::Format f, const char *name) -> bool {
        if (m_sc->isFormatSupported(f)) {
            m_sc->setFormat(f);
            m_hdrActive = (f != QRhiSwapChain::SDR);
            qInfo("vantaviewer: swapchain format = %s%s", name, m_hdrActive ? " (HDR)" : "");
            return true;
        }
        return false;
    };

    bool ok = false;
    if (want == "hdr10")
        ok = trySet(QRhiSwapChain::HDR10, "HDR10/PQ");
    else if (want == "sdr")
        ok = trySet(QRhiSwapChain::SDR, "SDR");
    else if (want == "p3")
        ok = trySet(QRhiSwapChain::HDRExtendedDisplayP3Linear, "HDRExtendedDisplayP3Linear");
    else
        ok = trySet(QRhiSwapChain::HDRExtendedSrgbLinear, "HDRExtendedSrgbLinear/scRGB");

    if (!ok) {
        m_sc->setFormat(QRhiSwapChain::SDR);
        m_hdrActive = false;
        qWarning("vantaviewer: HDR format unsupported -> SDR");
    }

    m_rp.reset(m_sc->newCompatibleRenderPassDescriptor());
    m_sc->setRenderPassDescriptor(m_rp.get());

    bool okScale = false;
    const float s = qEnvironmentVariable("VANTAVIEWER_SCALE").toFloat(&okScale);
    if (okScale) m_scale = s;

    const QSize firstSize = (m_image && m_image->valid()) ? QSize(m_image->w, m_image->h)
                                                          : QSize(1, 1);
    m_tex.reset(m_rhi->newTexture(QRhiTexture::RGBA16F, firstSize));
    m_tex->create();

    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_sampler->create();
    m_ubo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 8 * sizeof(float)));
    m_ubo->create();

    m_srb.reset(m_rhi->newShaderResourceBindings());
    m_srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::FragmentStage, m_ubo.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  m_tex.get(), m_sampler.get())
    });
    m_srb->create();

    QRhiVertexInputLayout inputLayout; // fullscreen triangle from gl_VertexIndex
    m_ps.reset(m_rhi->newGraphicsPipeline());
    m_ps->setShaderStages({
        { QRhiShaderStage::Vertex,   loadShader(QStringLiteral(":/shaders/fullscreen.vert.qsb")) },
        { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/image.frag.qsb")) }
    });
    m_ps->setVertexInputLayout(inputLayout);
    m_ps->setShaderResourceBindings(m_srb.get());
    m_ps->setRenderPassDescriptor(m_rp.get());
    if (!m_ps->create())
        qFatal("vantaviewer: failed to create image pipeline");

    initOverlay();

    m_initialized = true;
}

void ViewerWindow::initOverlay()
{
    m_ovUbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 4 * sizeof(float)));
    m_ovUbo->create();
    m_ovVbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 16 * sizeof(float)));
    m_ovVbuf->create();
    m_cropVbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 16 * sizeof(float)));
    m_cropVbuf->create();
    m_toastVbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 16 * sizeof(float)));
    m_toastVbuf->create();
    m_panelTex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    m_panelTex->create();
    m_cropTex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    m_cropTex->create();
    m_toastTex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    m_toastTex->create();

    m_ovSrb.reset(m_rhi->newShaderResourceBindings());
    m_ovSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::FragmentStage, m_ovUbo.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  m_panelTex.get(), m_sampler.get())
    });
    m_ovSrb->create();

    m_ovSrbCrop.reset(m_rhi->newShaderResourceBindings());
    m_ovSrbCrop->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::FragmentStage, m_ovUbo.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  m_cropTex.get(), m_sampler.get())
    });
    m_ovSrbCrop->create();

    m_ovSrbToast.reset(m_rhi->newShaderResourceBindings());
    m_ovSrbToast->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::FragmentStage, m_ovUbo.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  m_toastTex.get(), m_sampler.get())
    });
    m_ovSrbToast->create();

    QRhiVertexInputLayout layout;
    layout.setBindings({ { 4 * sizeof(float) } });
    layout.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
        { 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) }
    });

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;

    m_ovPs.reset(m_rhi->newGraphicsPipeline());
    m_ovPs->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_ovPs->setTargetBlends({ blend });
    m_ovPs->setShaderStages({
        { QRhiShaderStage::Vertex,   loadShader(QStringLiteral(":/shaders/overlay.vert.qsb")) },
        { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/overlay.frag.qsb")) }
    });
    m_ovPs->setVertexInputLayout(layout);
    m_ovPs->setShaderResourceBindings(m_ovSrb.get());
    m_ovPs->setRenderPassDescriptor(m_rp.get());
    if (!m_ovPs->create())
        qFatal("vantaviewer: failed to create overlay pipeline");
}

void ViewerWindow::resizeSwapChain()
{
    m_hasSwapChain = m_sc->createOrResize();
}

void ViewerWindow::releaseSwapChain()
{
    if (m_hasSwapChain) {
        m_hasSwapChain = false;
        m_sc->destroy();
    }
}

void ViewerWindow::render()
{
    if (!m_hasSwapChain || !m_initialized)
        return;

    if (m_sc->currentPixelSize() != m_sc->surfacePixelSize()) {
        resizeSwapChain();
        if (!m_hasSwapChain)
            return;
    }

    QRhi::FrameOpResult r = m_rhi->beginFrame(m_sc.get());
    if (r == QRhi::FrameOpSwapChainOutOfDate) {
        resizeSwapChain();
        if (!m_hasSwapChain)
            return;
        r = m_rhi->beginFrame(m_sc.get());
    }
    if (r != QRhi::FrameOpSuccess) {
        requestUpdate();
        return;
    }

    QRhiCommandBuffer *cb = m_sc->currentFrameCommandBuffer();
    const QSize outputSize = m_sc->currentPixelSize();

    QRhiResourceUpdateBatch *rub = m_rhi->nextResourceUpdateBatch();

    if (m_incomingDirty && m_image && m_image->valid()) {
        resizeTexture(*m_image);
        QRhiTextureSubresourceUploadDescription sub(
            m_image->rgba16f.data(), quint32(m_image->rgba16f.size() * sizeof(uint16_t)));
        QRhiTextureUploadEntry entry(0, 0, sub);
        QRhiTextureUploadDescription desc(entry);
        rub->uploadTexture(m_tex.get(), desc);
        m_incomingDirty = false;
        m_firstShown = true;
    }

    m_view.setWindowSize(outputSize);
    float usx = 1.0f, usy = 1.0f, uox = 0.0f, uoy = 0.0f;
    m_view.uvScaleOffset(usx, usy, uox, uoy);
    // Use the HDR shader path only when the monitor is HDR AND we actually got an HDR
    // swapchain. On an SDR display (e.g. a laptop) this falls back to correct sRGB.
    const float sdrFlag = (m_hdrMode && m_hdrActive) ? 0.0f : 1.0f;
    const float imageHdr = (m_image && m_image->hdr) ? 1.0f : 0.0f;
    const float ubo[8] = {
        m_scale, sdrFlag, imageHdr, float(m_view.rotation()),
        usx, usy, uox, uoy
    };
    rub->updateDynamicBuffer(m_ubo.get(), 0, sizeof(ubo), ubo);

    // Prepare overlays (crop chrome under, info card + toast on top) before the pass.
    const bool drawInfo = m_infoVisible && !m_panelImage.isNull() && m_firstShown;
    const bool drawCrop = m_crop.active() && m_firstShown;
    const bool drawToast = m_toastVisible && !m_toastImage.isNull() && m_firstShown;
    const float W = float(outputSize.width()), H = float(outputSize.height());
    auto ndx = [&](float x) { return x / W * 2.0f - 1.0f; };
    auto ndy = [&](float y) { return y / H * 2.0f - 1.0f; };

    if (drawInfo || drawCrop || drawToast) {
        const float ov[4] = { m_scale, sdrFlag, 0.0f, 0.0f };
        rub->updateDynamicBuffer(m_ovUbo.get(), 0, sizeof(ov), ov);
    }

    if (drawCrop) {
        // Map the crop rect (image px) into screen px via the 4 corners' bounding box,
        // rebuild the chrome at window size, and upload.
        const QRectF cr = m_crop.rect();
        const QPointF p0 = m_view.imageToScreen(cr.topLeft());
        const QPointF p1 = m_view.imageToScreen(cr.topRight());
        const QPointF p2 = m_view.imageToScreen(cr.bottomRight());
        const QPointF p3 = m_view.imageToScreen(cr.bottomLeft());
        const double minx = std::min({ p0.x(), p1.x(), p2.x(), p3.x() });
        const double maxx = std::max({ p0.x(), p1.x(), p2.x(), p3.x() });
        const double miny = std::min({ p0.y(), p1.y(), p2.y(), p3.y() });
        const double maxy = std::max({ p0.y(), p1.y(), p2.y(), p3.y() });
        const QRectF screenRect(minx, miny, maxx - minx, maxy - miny);
        const bool freeform = (m_crop.ratio() == CropState::Ratio::Free);
        const QString label = QStringLiteral("%1  ·  %2×%3").arg(m_crop.ratioName())
                                  .arg(qRound(cr.width())).arg(qRound(cr.height()));
        const QImage chrome = m_cropOv.build(outputSize, screenRect, freeform, label,
                                             devicePixelRatio());
        if (m_cropTex->pixelSize() != chrome.size()) {
            m_cropTex->destroy();
            m_cropTex->setPixelSize(chrome.size());
            m_cropTex->create();
        }
        QRhiTextureSubresourceUploadDescription sub(chrome);
        QRhiTextureUploadEntry entry(0, 0, sub);
        rub->uploadTexture(m_cropTex.get(), QRhiTextureUploadDescription(entry));

        const float fw[16] = {
            -1.0f, -1.0f, 0.0f, 0.0f,   1.0f, -1.0f, 1.0f, 0.0f,
            -1.0f,  1.0f, 0.0f, 1.0f,   1.0f,  1.0f, 1.0f, 1.0f
        };
        rub->updateDynamicBuffer(m_cropVbuf.get(), 0, sizeof(fw), fw);
    }

    if (drawInfo) {
        if (m_panelDirty) {
            const QImage img = m_panelImage.convertToFormat(QImage::Format_RGBA8888);
            if (m_panelTex->pixelSize() != img.size()) {
                m_panelTex->destroy();
                m_panelTex->setPixelSize(img.size());
                m_panelTex->create();
            }
            QRhiTextureSubresourceUploadDescription sub(img);
            QRhiTextureUploadEntry entry(0, 0, sub);
            rub->uploadTexture(m_panelTex.get(), QRhiTextureUploadDescription(entry));
            m_panelDirty = false;
        }
        const float margin = 24.0f;
        const float pw = float(m_panelTex->pixelSize().width());
        const float ph = float(m_panelTex->pixelSize().height());
        const float x0 = ndx(margin), y0 = ndy(margin);
        const float x1 = ndx(margin + pw), y1 = ndy(margin + ph);
        const float verts[16] = {
            x0, y0, 0.0f, 0.0f,  x1, y0, 1.0f, 0.0f,
            x0, y1, 0.0f, 1.0f,  x1, y1, 1.0f, 1.0f
        };
        rub->updateDynamicBuffer(m_ovVbuf.get(), 0, sizeof(verts), verts);
    }

    if (drawToast) {
        if (m_toastDirty) {
            const QImage img = m_toastImage.convertToFormat(QImage::Format_RGBA8888);
            if (m_toastTex->pixelSize() != img.size()) {
                m_toastTex->destroy();
                m_toastTex->setPixelSize(img.size());
                m_toastTex->create();
            }
            QRhiTextureSubresourceUploadDescription sub(img);
            QRhiTextureUploadEntry entry(0, 0, sub);
            rub->uploadTexture(m_toastTex.get(), QRhiTextureUploadDescription(entry));
            m_toastDirty = false;
        }
        const float pw = float(m_toastTex->pixelSize().width());
        const float ph = float(m_toastTex->pixelSize().height());
        const float bx = (W - pw) * 0.5f;
        // Prompts sit centred; transient toasts sit near the bottom.
        const float by = m_cardCentered ? (H - ph) * 0.5f
                                        : H - 32.0f * float(devicePixelRatio()) - ph;
        const float x0 = ndx(bx), y0 = ndy(by);
        const float x1 = ndx(bx + pw), y1 = ndy(by + ph);
        const float verts[16] = {
            x0, y0, 0.0f, 0.0f,  x1, y0, 1.0f, 0.0f,
            x0, y1, 0.0f, 1.0f,  x1, y1, 1.0f, 1.0f
        };
        rub->updateDynamicBuffer(m_toastVbuf.get(), 0, sizeof(verts), verts);
    }

    cb->beginPass(m_sc->currentFrameRenderTarget(), QColor(0, 0, 0, 255), { 1.0f, 0 }, rub);
    if (m_firstShown) {
        cb->setGraphicsPipeline(m_ps.get());
        cb->setViewport({ 0, 0, float(outputSize.width()), float(outputSize.height()) });
        cb->setShaderResources();
        cb->draw(3);
    }
    if (drawCrop) {
        cb->setGraphicsPipeline(m_ovPs.get());
        cb->setViewport({ 0, 0, float(outputSize.width()), float(outputSize.height()) });
        cb->setShaderResources(m_ovSrbCrop.get());
        const QRhiCommandBuffer::VertexInput vbuf(m_cropVbuf.get(), 0);
        cb->setVertexInput(0, 1, &vbuf);
        cb->draw(4);
    }
    if (drawInfo) {
        cb->setGraphicsPipeline(m_ovPs.get());
        cb->setViewport({ 0, 0, float(outputSize.width()), float(outputSize.height()) });
        cb->setShaderResources(m_ovSrb.get());
        const QRhiCommandBuffer::VertexInput vbuf(m_ovVbuf.get(), 0);
        cb->setVertexInput(0, 1, &vbuf);
        cb->draw(4);
    }
    if (drawToast) {
        cb->setGraphicsPipeline(m_ovPs.get());
        cb->setViewport({ 0, 0, float(outputSize.width()), float(outputSize.height()) });
        cb->setShaderResources(m_ovSrbToast.get());
        const QRhiCommandBuffer::VertexInput vbuf(m_toastVbuf.get(), 0);
        cb->setVertexInput(0, 1, &vbuf);
        cb->draw(4);
    }
    cb->endPass();

    m_rhi->endFrame(m_sc.get());
}

void ViewerWindow::rebuildInfoPanel()
{
    if (!m_image || !m_image->valid())
        return;
    static const QList<QPair<QString, QString>> kActions = {
        { QStringLiteral("next"), QStringLiteral("Next") },
        { QStringLiteral("prev"), QStringLiteral("Previous") },
        { QStringLiteral("fit"), QStringLiteral("Fit") },
        { QStringLiteral("oneToOne"), QStringLiteral("1:1") },
        { QStringLiteral("rotateCW"), QStringLiteral("Rotate right") },
        { QStringLiteral("rotateCCW"), QStringLiteral("Rotate left") },
        { QStringLiteral("rotate180"), QStringLiteral("Rotate 180°") },
        { QStringLiteral("crop"), QStringLiteral("Crop") },
        { QStringLiteral("cropRatio"), QStringLiteral("Crop ratio +") },
        { QStringLiteral("cropRatioPrev"), QStringLiteral("Crop ratio −") },
        { QStringLiteral("save"), QStringLiteral("Save") },
        { QStringLiteral("saveAs"), QStringLiteral("Save as") },
        { QStringLiteral("info"), QStringLiteral("Info") },
        { QStringLiteral("fullscreen"), QStringLiteral("Fullscreen") },
        { QStringLiteral("quit"), QStringLiteral("Quit") },
    };
    QList<QPair<QString, QString>> keys;
    for (const auto &a : kActions) {
        const QString disp = m_keys.primaryChordDisplay(a.first);
        if (!disp.isEmpty())
            keys.append({ disp, a.second });
    }
    m_panelImage = m_info.build(m_currentPath, *m_image, m_hdrMode,
                                m_playlist.currentIndex() + 1, m_playlist.size(),
                                keys, devicePixelRatio());
    m_panelDirty = true;
}

static QImage buildToastCard(const QString &text, qreal dpr)
{
    QFont font;
    font.setPointSizeF(11.0);
    const QFontMetrics fm(font);
    const int pad = 12;
    const int w = fm.horizontalAdvance(text) + pad * 2;
    const int h = fm.height() + pad;

    QImage card(int(w * dpr), int(h * dpr), QImage::Format_RGBA8888);
    card.setDevicePixelRatio(dpr);
    card.fill(Qt::transparent);
    QPainter p(&card);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 190));
    p.drawRoundedRect(QRectF(0, 0, w, h), 8, 8);
    p.setFont(font);
    p.setPen(QColor(245, 245, 245));
    p.drawText(QRectF(0, 0, w, h), Qt::AlignCenter, text);
    p.end();
    return card;
}

void ViewerWindow::showToast(const QString &text)
{
    m_toastImage = buildToastCard(text, devicePixelRatio());
    m_toastDirty = true;
    m_toastVisible = true;
    m_cardCentered = false;
    m_toastTimer.start(2600);
    requestUpdate();
}

void ViewerWindow::beginSaveOverwrite()
{
    if (m_saving || m_currentPath.isEmpty() || m_inputMode != Input::None)
        return;
    const QFileInfo fi(m_currentPath);
    const bool hdr = m_image && m_image->hdr;
    if (!encoder::canEncodeExtension(fi.suffix(), hdr)) {
        showToast(QStringLiteral("Can't overwrite .%1 (encoding unsupported) — use Save as")
                      .arg(fi.suffix().toLower()));
        return;
    }
    m_inputMode = Input::ConfirmOverwrite;
    m_inputTarget = m_currentPath;
    rebuildPromptCard();
    requestUpdate();
}

void ViewerWindow::beginSaveAs()
{
    if (m_saving || m_currentPath.isEmpty() || m_inputMode != Input::None)
        return;
    m_inputMode = Input::SaveAs;
    m_inputText = m_currentPath; // pre-filled with the current path; edit freely
    rebuildPromptCard();
    requestUpdate();
}

void ViewerWindow::cancelPrompt()
{
    m_inputMode = Input::None;
    m_toastVisible = false;
    requestUpdate();
}

QString ViewerWindow::resolveSavePath(const QString &typed) const
{
    QString t = typed.trimmed();
    if (t.isEmpty())
        return QString();
    if (t.startsWith(QLatin1Char('~')))
        t = QDir::homePath() + t.mid(1);
    if (!t.contains(QLatin1Char('/'))) // bare filename -> current file's directory
        t = QFileInfo(m_currentPath).dir().filePath(t);
    return QFileInfo(t).absoluteFilePath();
}

void ViewerWindow::performSave(const QString &path)
{
    m_inputMode = Input::None;
    m_toastVisible = false;
    if (m_saving || path.isEmpty())
        return;
    auto img = m_loader.loadSync(m_currentPath); // guaranteed full-resolution
    if (!img || !img->valid()) {
        showToast(QStringLiteral("Cannot save: decode failed"));
        return;
    }

    QRect crop; // null == whole image
    if (m_crop.active()) {
        const QRectF r = m_crop.rect();
        crop = QRect(qRound(r.left()), qRound(r.top()), qRound(r.width()), qRound(r.height()));
    }
    const int rot = m_view.rotation();

    m_saving = true;
    showToast(QStringLiteral("Saving…"));

    const QString cur = m_currentPath;
    auto *watcher = new QFutureWatcher<encoder::Result>(this);
    connect(watcher, &QFutureWatcher<encoder::Result>::finished, this, [this, watcher, path, cur]() {
        const encoder::Result res = watcher->result();
        watcher->deleteLater();
        m_saving = false;
        if (res.ok) {
            showToast(QStringLiteral("Saved %1").arg(QFileInfo(res.message).fileName()));
            // Rescan so a newly written sibling shows up in navigation.
            m_playlist.reload();
            m_playlist.setCurrentPath(cur);
            updateHotSet();
        } else {
            showToast(QStringLiteral("Save failed: %1").arg(res.message));
        }
    });
    watcher->setFuture(QtConcurrent::run([img, path, crop, rot]() {
        return encoder::encode(path, *img, crop, rot);
    }));
}

void ViewerWindow::rebuildPromptCard()
{
    QString label;
    if (m_inputMode == Input::ConfirmOverwrite) {
        label = QStringLiteral("Overwrite %1 ?    Enter = yes    Esc = no")
                    .arg(QFileInfo(m_inputTarget).fileName());
    } else if (m_inputMode == Input::SaveAs) {
        label = QStringLiteral("Save as:  %1▏    Enter = save    Esc = cancel")
                    .arg(m_inputText);
    } else {
        return;
    }
    m_toastImage = buildToastCard(label, devicePixelRatio());
    m_toastDirty = true;
    m_toastVisible = true;
    m_cardCentered = true;
    m_toastTimer.stop(); // a prompt stays until answered
}

void ViewerWindow::exposeEvent(QExposeEvent *)
{
    if (isExposed() && !m_initialized) {
        init();
        resizeSwapChain();
    }

    const QSize surfaceSize = m_hasSwapChain ? m_sc->surfacePixelSize() : QSize();

    if (isExposed() && m_initialized && m_hasSwapChain && !surfaceSize.isEmpty()) {
        // Build the (default-on) info card now that the real device-pixel-ratio is known.
        if (m_infoVisible && m_panelImage.isNull())
            rebuildInfoPanel();

        render();

        if (!m_colorApplied) {
            m_colorApplied = true;
            m_color = std::make_unique<cm::SurfaceColor>(this);
            if (m_color->valid()) {
                if (m_hdrMode)
                    m_color->setWindowsScrgb();
                else
                    m_color->setSrgb();
            }
            render(); // commit applies the image description
        }
    }

    if (!isExposed() && m_initialized && surfaceSize.isEmpty())
        releaseSwapChain();
}

bool ViewerWindow::event(QEvent *e)
{
    switch (e->type()) {
    case QEvent::UpdateRequest:
        render();
        break;
    case QEvent::PlatformSurface:
        if (static_cast<QPlatformSurfaceEvent *>(e)->surfaceEventType()
            == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
            releaseSwapChain();
        break;
    default:
        break;
    }
    return QWindow::event(e);
}

void ViewerWindow::keyPressEvent(QKeyEvent *e)
{
    // Modal save prompt swallows all keys until answered.
    if (m_inputMode != Input::None) {
        if (e->key() == Qt::Key_Escape) {
            cancelPrompt();
        } else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            if (m_inputMode == Input::ConfirmOverwrite)
                performSave(m_inputTarget);
            else
                performSave(resolveSavePath(m_inputText));
        } else if (m_inputMode == Input::SaveAs) {
            if (e->key() == Qt::Key_Backspace)
                m_inputText.chop(1);
            else if (!e->text().isEmpty() && e->text().at(0).isPrint())
                m_inputText += e->text();
            rebuildPromptCard();
            requestUpdate();
        }
        return;
    }

    // Escape cancels crop mode rather than quitting, when active.
    if (m_crop.active() && e->key() == Qt::Key_Escape) {
        m_crop.setActive(false);
        requestUpdate();
        return;
    }

    const QPointF centre(width() * devicePixelRatio() * 0.5,
                         height() * devicePixelRatio() * 0.5);
    const QString action = m_keys.actionFor(e);

    if (action == QLatin1String("fullscreen")) {
        setVisibility(visibility() == QWindow::FullScreen ? QWindow::Windowed
                                                          : QWindow::FullScreen);
    } else if (action == QLatin1String("oneToOne")) {
        m_view.setOneToOne(centre);
        requestUpdate();
    } else if (action == QLatin1String("fit")) {
        m_view.fit();
        requestUpdate();
    } else if (action == QLatin1String("rotateCW")) {
        m_view.rotateBy(1);
        requestUpdate();
    } else if (action == QLatin1String("rotateCCW")) {
        m_view.rotateBy(-1);
        requestUpdate();
    } else if (action == QLatin1String("rotate180")) {
        m_view.rotateBy(2);
        requestUpdate();
    } else if (action == QLatin1String("next")) {
        navigate(+1);
    } else if (action == QLatin1String("prev")) {
        navigate(-1);
    } else if (action == QLatin1String("info")) {
        m_infoVisible = !m_infoVisible;
        if (m_infoVisible)
            rebuildInfoPanel();
        requestUpdate();
    } else if (action == QLatin1String("crop")) {
        if (m_image && m_image->valid()) {
            if (m_crop.active()) {
                m_crop.setActive(false);
            } else {
                m_crop.begin(QSize(m_image->w, m_image->h));
                m_crop.setActive(true);
            }
            requestUpdate();
        }
    } else if (action == QLatin1String("cropRatio")) {
        if (m_crop.active()) { m_crop.cycleRatio(+1); requestUpdate(); }
    } else if (action == QLatin1String("cropRatioPrev")) {
        if (m_crop.active()) { m_crop.cycleRatio(-1); requestUpdate(); }
    } else if (action == QLatin1String("save")) {
        beginSaveOverwrite();
    } else if (action == QLatin1String("saveAs")) {
        beginSaveAs();
    } else if (action == QLatin1String("quit")) {
        QGuiApplication::quit();
    } else {
        QWindow::keyPressEvent(e);
    }
}

void ViewerWindow::wheelEvent(QWheelEvent *e)
{
    const int dy = e->angleDelta().y();
    if (dy == 0)
        return;
    const double factor = std::pow(1.0015, double(dy)); // ~1.2x per notch (120 units)
    m_view.zoomAt(devicePos(e->position()), factor);
    requestUpdate();
}

void ViewerWindow::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (m_crop.active()) {
        // Grab radius: ~11 screen px expressed in image px.
        const double tolImg = 11.0 * devicePixelRatio()
                              / std::max(1e-4, m_view.effectiveScale());
        m_crop.press(m_view.screenToImage(devicePos(e->position())), tolImg);
        m_cropMouseDown = true;
        return;
    }
    m_panning = true;
    m_lastMousePos = devicePos(e->position());
    setCursor(Qt::ClosedHandCursor);
}

void ViewerWindow::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (m_cropMouseDown) {
        m_crop.release();
        m_cropMouseDown = false;
        return;
    }
    if (m_panning) {
        m_panning = false;
        unsetCursor();
    }
}

void ViewerWindow::mouseMoveEvent(QMouseEvent *e)
{
    if (m_cropMouseDown) {
        if (m_crop.heldHandle() != CropState::Handle::None) {
            m_crop.dragTo(m_view.screenToImage(devicePos(e->position())));
            requestUpdate();
        }
        return;
    }
    if (!m_panning)
        return;
    const QPointF pos = devicePos(e->position());
    m_view.panBy(pos - m_lastMousePos);
    m_lastMousePos = pos;
    requestUpdate();
}

void ViewerWindow::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (m_crop.active())
        return; // double-click is reserved for crop interaction
    // Toggle between fit and 1:1, anchored on the cursor.
    if (m_view.isFit())
        m_view.setOneToOne(devicePos(e->position()));
    else
        m_view.fit();
    requestUpdate();
}
