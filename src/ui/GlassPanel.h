#pragma once

// GlassPanel：毛玻璃圆角面板。
// 绘制时从 WallpaperBackground 取当前壁纸的模糊版本，
// 裁出本面板覆盖的区域，叠加半透明蒙层与圆角高光描边，形成磨砂质感。

#include <QColor>
#include <QFrame>

namespace gc {

class WallpaperBackground;

class GlassPanel : public QFrame {
    Q_OBJECT
public:
    explicit GlassPanel(QWidget* parent = nullptr);

    void setTint(const QColor& tint);
    QColor tint() const { return m_tint; }
    void setCornerRadius(int r) { m_radius = r; update(); }

protected:
    void paintEvent(QPaintEvent* e) override;
    void showEvent(QShowEvent* e) override;

private:
    WallpaperBackground* findBackground() const;

    QColor m_tint{ 255, 255, 255, 70 };
    int m_radius = 16;
    bool m_connected = false;
};

} // namespace gc
