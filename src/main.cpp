#include <QGuiApplication>
#include <QVulkanInstance>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QIcon>
#include <rhi/qrhi.h>

#include "viewer_window.h"

#include <cstdio>

// Query Hyprland for the focused monitor's colour-management preset, so the
// window tags + renders correctly from the first frame. The per-window live poll
// (a later phase) keeps it in sync as the user toggles HDR. Defaults to HDR if
// hyprctl is unavailable; the swapchain still falls back to SDR when unsupported.
static bool focusedMonitorIsHdr()
{
    QProcess p;
    p.start(QStringLiteral("hyprctl"), { QStringLiteral("monitors"), QStringLiteral("-j") });
    if (!p.waitForFinished(500)) {
        p.kill();
        return true;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(p.readAllStandardOutput());
    if (!doc.isArray())
        return true;
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject m = v.toObject();
        if (m.value(QStringLiteral("focused")).toBool())
            return m.value(QStringLiteral("colorManagementPreset")).toString()
                   == QLatin1String("hdr");
    }
    return true;
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("vantaviewer"));
    // Wayland app_id -> matches the installed vantaviewer.desktop (icon, grouping).
    app.setDesktopFileName(QStringLiteral("vantaviewer"));
    QIcon icon;
    icon.addFile(QStringLiteral(":/icons/vantaviewer-256.png"), QSize(256, 256));
    icon.addFile(QStringLiteral(":/icons/vantaviewer-128.png"), QSize(128, 128));
    icon.addFile(QStringLiteral(":/icons/vantaviewer-64.png"), QSize(64, 64));
    app.setWindowIcon(icon);

    if (argc < 2) {
        std::fprintf(stderr, "usage: vantaviewer <image>\n");
        return 2;
    }
    const QString path = QString::fromLocal8Bit(argv[1]);

    QVulkanInstance inst;
    inst.setExtensions(QRhiVulkanInitParams::preferredInstanceExtensions());
    if (!inst.create())
        qFatal("vantaviewer: failed to create Vulkan instance");

    const bool hdr = focusedMonitorIsHdr();

    ViewerWindow w(&inst, hdr);
    if (!w.openPath(path)) {
        std::fprintf(stderr, "vantaviewer: could not decode %s\n", qPrintable(path));
        return 1;
    }
    w.show();

    return app.exec();
}
