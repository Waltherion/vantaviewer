#pragma once

#include <QSize>
#include <QPointF>

// All of the viewer's fit/zoom/pan/rotation maths in one place. It maps between
// three spaces, all in DEVICE pixels:
//   * window pixels      -- the swapchain surface (0..W, 0..H)
//   * display space      -- the image as the user sees it (after rotation)
//   * texture/image space-- the decoded texture (unrotated, 0..imgW/imgH)
//
// Rendering consumes uvScaleOffset() + rotation(): the shader maps window-uv to
// display-uv with (scale, offset) and then swizzles display-uv to texture-uv by
// the rotation quadrant. screenToImage()/imageToScreen() are exact inverses and
// are what the crop overlay (later phase) uses to keep handles glued to the image
// through any zoom/pan/rotation.
class ViewTransform
{
public:
    void setImageSize(QSize s) { m_image = s; }
    void setWindowSize(QSize s) { m_window = s; clampPan(); }
    QSize imageSize() const { return m_image; }
    QSize windowSize() const { return m_window; }

    int rotation() const { return m_rot; }            // 0,1,2,3 == 0/90/180/270 CW
    void setRotation(int quadrant);
    void rotateBy(int quadrants);                     // +1 CW, -1 CCW, +2 180

    void fit();                                       // zoom = 1 (fit), pan = 0
    void setOneToOne(QPointF anchorPx);               // 1 texture px == 1 screen px
    void zoomAt(QPointF cursorPx, double factor);     // keep the point under cursor fixed
    void panBy(QPointF deltaPx);

    double zoom() const { return m_zoom; }
    bool isFit() const { return m_zoom == 1.0 && m_pan.isNull(); }

    QSizeF displaySize() const;                       // rotated image dimensions
    double fitScale() const;                          // screen px per display px at zoom 1
    double effectiveScale() const { return fitScale() * m_zoom; }

    // window-uv -> display-uv:  duv = (wuv - 0.5) * uvScale + 0.5 + uvOffset
    void uvScaleOffset(float &usx, float &usy, float &uox, float &uoy) const;

    QPointF screenToImage(QPointF windowPx) const;    // window px -> texture px
    QPointF imageToScreen(QPointF imagePx) const;     // texture px -> window px

private:
    void clampPan();

    QSize m_image;
    QSize m_window;
    int m_rot = 0;
    double m_zoom = 1.0;
    QPointF m_pan; // window-px offset of the image centre from the window centre
};
