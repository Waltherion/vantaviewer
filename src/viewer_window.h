#pragma once

#include <QWindow>
#include <rhi/qrhi.h>
#include <memory>

#include "hdr_image.h"
#include "cm_tagging.h"
#include "view_transform.h"
#include "keybindings.h"
#include "playlist.h"
#include "image_loader.h"
#include "hdr_monitor.h"
#include "info_overlay.h"
#include "crop_state.h"
#include "crop_overlay.h"

#include <QImage>
#include <QTimer>

class QVulkanInstance;

// A normal application window (xdg-toplevel) that owns its own QRhi + Vulkan
// swapchain so it can request an HDR (scRGB extended-linear) format and tag the
// surface via wp-color-management -- exactly like vantapaper's wallpaper output,
// but a regular focusable window that takes keyboard/pointer input.
//
// Shows ONE image, correct in both HDR and SDR monitor modes, with fit/zoom/pan
// and 90/180 rotation (all in ViewTransform). Navigation/crop come later.
class ViewerWindow : public QWindow
{
    Q_OBJECT
public:
    ViewerWindow(QVulkanInstance *inst, bool hdrMode);
    ~ViewerWindow() override;

    // Open a file: load its directory as the playlist, position on it, show it, and
    // start prefetching neighbours + live HDR/SDR polling. Returns false if the file
    // could not be decoded.
    bool openPath(const QString &path);

    // Tell the window whether its monitor is currently in HDR or SDR mode, so it
    // re-tags the surface and the shader's SDR branch follows.
    void setHdrMode(bool hdr);

protected:
    void exposeEvent(QExposeEvent *) override;
    bool event(QEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;

private:
    void init();
    void initOverlay();
    void resizeSwapChain();
    void releaseSwapChain();
    void render();
    void resizeTexture(const HdrImage &img);
    void rebuildInfoPanel(); // refresh the info card from the current image/mode
    void showToast(const QString &text);

    // Save flow. Ctrl+S overwrites the current file (with a confirm prompt, preserving
    // its format); Ctrl+Shift+S opens an in-place text field to type a target path.
    void beginSaveOverwrite();
    void beginSaveAs();
    // Async encode of the working image (crop + rotation baked) to path, then toast.
    // updateCurrent=true bakes the result into the view + cache (overwrite); false is
    // a pure export to another file (save-as).
    void performSave(const QString &path, bool updateCurrent);
    void rebuildPromptCard();              // (re)render the modal card
    void cancelPrompt();
    QString resolveSavePath(const QString &typed) const;
    QPointF devicePos(QPointF logical) const { return logical * devicePixelRatio(); }

    // Display a decoded image. resetView=true reframes (fit, rotation cleared) for a
    // new picture; false keeps zoom/pan/rotation for an in-place full-res upgrade.
    void setImage(std::shared_ptr<const HdrImage> image, bool resetView);
    bool showPath(const QString &path); // current playlist item -> screen
    void navigate(int dir);             // +1 next, -1 previous (prompts if edited)
    void doNavigate(int dir);           // actually move (after any save/discard)
    void updateHotSet();                // pin current + neighbours in the loader
    void commitCrop();                  // bake the active crop into the working image

    QVulkanInstance *m_inst = nullptr;

    // Declaration order matters: m_rhi must outlive the resources below.
    std::unique_ptr<QRhi> m_rhi;
    std::unique_ptr<QRhiSwapChain> m_sc;
    std::unique_ptr<QRhiRenderPassDescriptor> m_rp;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline> m_ps;
    std::unique_ptr<QRhiTexture> m_tex;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiBuffer> m_ubo;

    // Overlay pipeline (info card + crop chrome): textured quad, alpha-blended.
    std::unique_ptr<QRhiShaderResourceBindings> m_ovSrb;     // binds the info panel
    std::unique_ptr<QRhiShaderResourceBindings> m_ovSrbCrop; // binds the crop chrome
    std::unique_ptr<QRhiGraphicsPipeline> m_ovPs;
    std::unique_ptr<QRhiBuffer> m_ovUbo;
    std::unique_ptr<QRhiShaderResourceBindings> m_ovSrbToast; // binds the toast card
    std::unique_ptr<QRhiBuffer> m_ovVbuf;     // info panel quad
    std::unique_ptr<QRhiBuffer> m_cropVbuf;   // full-window crop chrome quad
    std::unique_ptr<QRhiBuffer> m_toastVbuf;  // toast quad
    std::unique_ptr<QRhiTexture> m_panelTex;
    std::unique_ptr<QRhiTexture> m_cropTex;
    std::unique_ptr<QRhiTexture> m_toastTex;

    std::shared_ptr<const HdrImage> m_image;
    bool m_incomingDirty = false;
    bool m_firstShown = false;

    ViewTransform m_view;
    KeyBindings m_keys;
    Playlist m_playlist;
    ImageLoader m_loader;
    HdrMonitor m_hdrMon;
    InfoOverlay m_info;
    CropState m_crop;
    CropOverlay m_cropOv;
    QString m_currentPath;
    bool m_panning = false;
    bool m_cropMouseDown = false;
    QPointF m_lastMousePos; // device px

    bool m_infoVisible = true;  // info card shown by default; toggle off with `i`
    QImage m_panelImage;       // pending RGBA8 panel to upload
    bool m_panelDirty = false; // m_panelImage needs (re)uploading

    // One on-screen card slot, shared by transient toasts and the modal save prompt.
    bool m_toastVisible = false;
    QImage m_toastImage;
    bool m_toastDirty = false;
    bool m_cardCentered = false; // prompts render centred; toasts at the bottom
    QTimer m_toastTimer;
    bool m_saving = false;       // an async export is in flight

    enum class Input { None, ConfirmOverwrite, SaveAs, ConfirmSaveNav };
    Input m_inputMode = Input::None;
    QString m_inputText;   // editable path while in SaveAs
    QString m_inputTarget; // path to overwrite while in ConfirmOverwrite

    bool m_edited = false; // working image has unsaved crop/rotation edits
    int m_pendingNav = 0;  // navigation queued behind a save (set with ConfirmSaveNav)

    float m_scale = 2.5375f; // 203/80

    std::unique_ptr<cm::SurfaceColor> m_color;
    bool m_colorApplied = false;

    bool m_initialized = false;
    bool m_hasSwapChain = false;
    bool m_hdrActive = false; // swapchain got an HDR format

    bool m_hdrMode = true;    // monitor currently in HDR mode?
};
