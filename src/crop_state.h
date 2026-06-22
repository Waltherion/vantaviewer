#pragma once

#include <QRectF>
#include <QSize>
#include <QString>

// The crop rectangle, stored in IMAGE pixel coordinates so it survives zoom, pan
// and rotation. Phase 3 only defines/edits the rectangle (visual); writing it out
// is phase 4. A locked aspect ratio exposes the four corner handles (ratio-keeping);
// freeform exposes all eight handles.
class CropState
{
public:
    enum class Ratio { Free, Original, R16_9, R21_9, R4_3, R3_2, R1_1, R9_16, R16_10 };
    enum class Handle { None, TL, T, TR, R, BR, B, BL, L, Inside };

    void begin(const QSize &imageSize); // reset rect to the full image, keep ratio
    bool active() const { return m_active; }
    void setActive(bool a) { m_active = a; }
    void setImageSize(const QSize &s);  // clamp rect into a new image (rotate / new file)
    void rescale(const QSize &s);       // scale rect proportionally (full-res upgrade)

    QRectF rect() const { return m_rect; }
    QSize imageSize() const { return m_imageSize; }
    Ratio ratio() const { return m_ratio; }
    QString ratioName() const;
    void cycleRatio(int dir);           // step presets and re-fit the rect

    // Hit-test in image px; tolImg is the handle grab radius in image px. Edge
    // handles are only offered in freeform; corners + Inside always.
    Handle hitTest(const QPointF &imgPt, double tolImg) const;

    void press(const QPointF &imgPt, double tolImg); // pick a handle / inside / new rect
    void dragTo(const QPointF &imgPt);               // update rect from the held handle
    void release();
    Handle heldHandle() const { return m_drag; }

private:
    double ratioWH() const;             // target width/height; 0 == free
    void clampRect();
    void applyRatioAround(Handle anchorCorner);

    QSize m_imageSize;
    QRectF m_rect;
    Ratio m_ratio = Ratio::Free;
    bool m_active = false;

    Handle m_drag = Handle::None;
    QPointF m_dragStartImg;
    QRectF m_dragStartRect;
};
