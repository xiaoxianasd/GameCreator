#include "ui/GlassPanel.h"

#include "ui/WallpaperBackground.h"

#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QPainterPath>
#include <QShowEvent>

namespace gc {

GlassPanel::GlassPanel(QWidget* parent)
    : QFrame(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);

    // 柔和投影：让面板在浅色壁纸上也有清晰轮廓
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(32);
    shadow->setOffset(0, 6);
    shadow->setColor(QColor(50, 60, 90, 70));
    setGraphicsEffect(shadow);
}

void GlassPanel::setTint(const QColor& tint)
{
    m_tint = tint;
    update();
}

WallpaperBackground* GlassPanel::findBackground() const
{
    QWidget* w = parentWidget();
    while (w) {
        if (auto* bg = qobject_cast<WallpaperBackground*>(w))
            return bg;
        w = w->parentWidget();
    }
    return nullptr;
}

void GlassPanel::showEvent(QShowEvent* e)
{
    QFrame::showEvent(e);
    if (m_connected)
        return;
    if (WallpaperBackground* bg = findBackground()) {
        connect(bg, &WallpaperBackground::wallpaperChanged,
                this, qOverload<>(&QWidget::update), Qt::UniqueConnection);
        m_connected = true;
    }
}

void GlassPanel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), m_radius, m_radius);
    p.setClipPath(path);

    bool sampled = false;
    if (WallpaperBackground* bg = findBackground()) {
        const QImage blurred = bg->blurredWallpaper();
        if (!blurred.isNull()) {
            const QPoint topLeft = mapTo(bg, QPoint(0, 0));
            const QRect target(topLeft, size());
            const QRectF src = bg->coverSourceRect(blurred, target);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawImage(QRectF(QPointF(0, 0), QSizeF(size())), blurred, src);
            sampled = true;
        }
    }
    if (!sampled)
        p.fillRect(rect(), QColor(244, 246, 251, 225)); // 无壁纸时的浅色兜底

    // 半透明蒙层：提升亮度与磨砂感
    p.fillRect(rect(), m_tint);

    // 圆角描边：外圈淡灰（浅色壁纸上可见）+ 内圈高光
    p.setClipping(false);
    p.setPen(QPen(QColor(110, 120, 145, 90), 1));
    p.drawPath(path);
    QPainterPath inner;
    inner.addRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5),
                         m_radius - 1, m_radius - 1);
    p.setPen(QPen(QColor(255, 255, 255, 150), 1));
    p.drawPath(inner);
}

} // namespace gc
