#pragma once

#include <QObject>
#include <QTimer>
#include <QString>

// Polls Hyprland (`hyprctl monitors -j`) for the colour-management preset of the
// screen the window currently sits on, and emits hdrModeChanged when it flips. This
// is how the viewer re-tags + re-renders live as the user toggles HDR. Hyprland-only,
// which matches the rest of the colour-management stack.
class HdrMonitor : public QObject
{
    Q_OBJECT
public:
    explicit HdrMonitor(QObject *parent = nullptr);

    void setScreenName(const QString &name); // the screen the window is on
    void start(int intervalMs = 2000);
    void stop();

signals:
    void hdrModeChanged(bool hdr);

private:
    void poll();

    QTimer m_timer;
    QString m_screen;
    bool m_known = false;
    bool m_hdr = true;
};
