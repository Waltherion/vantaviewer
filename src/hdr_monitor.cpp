#include "hdr_monitor.h"

#include <QProcess>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

HdrMonitor::HdrMonitor(QObject *parent) : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &HdrMonitor::poll);
}

void HdrMonitor::setScreenName(const QString &name)
{
    if (m_screen == name)
        return;
    m_screen = name;
    m_known = false; // re-evaluate on the screen we just moved to
    poll();
}

void HdrMonitor::start(int intervalMs)
{
    poll();
    m_timer.start(intervalMs);
}

void HdrMonitor::stop()
{
    m_timer.stop();
}

void HdrMonitor::poll()
{
    if (m_screen.isEmpty())
        return;

    QProcess p;
    p.start(QStringLiteral("hyprctl"), { QStringLiteral("monitors"), QStringLiteral("-j") });
    if (!p.waitForFinished(500)) {
        p.kill();
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(p.readAllStandardOutput());
    if (!doc.isArray())
        return;

    for (const QJsonValue &v : doc.array()) {
        const QJsonObject m = v.toObject();
        if (m.value(QStringLiteral("name")).toString() != m_screen)
            continue;
        const bool hdr = m.value(QStringLiteral("colorManagementPreset")).toString()
                         == QLatin1String("hdr");
        if (!m_known || hdr != m_hdr) {
            m_known = true;
            m_hdr = hdr;
            emit hdrModeChanged(hdr);
        }
        return;
    }
}
