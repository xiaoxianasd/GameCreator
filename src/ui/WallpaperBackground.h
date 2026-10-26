#pragma once

// WallpaperBackground：主窗口背景层。
// - 在线获取动漫壁纸（多个公开图床轮询，自动跟随重定向）
// - 本地图片文件夹轮换（可单独使用，或作为在线失败时的兜底）
// - 定时交叉淡入淡出轮换
// - 为 GlassPanel 提供当前壁纸的高斯模糊版本（毛玻璃采样源）

#include <QImage>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QNetworkAccessManager;
class QTimer;
class QVariantAnimation;

namespace gc {

class WallpaperBackground : public QWidget {
    Q_OBJECT
public:
    // 背景来源：在线动漫壁纸 / 本地图片文件夹 / 在线优先、本地兜底
    enum SourceMode { Online = 0, Local = 1, OnlineFirst = 2 };

    explicit WallpaperBackground(QWidget* parent = nullptr);

    void setSourceMode(int mode) { m_mode = mode; }
    int sourceMode() const { return m_mode; }
    void setLocalDir(const QString& dir) { m_localDir = dir; }
    QString localDir() const { return m_localDir; }
    void setIntervalSeconds(int sec);
    int intervalSeconds() const { return m_intervalSec; }

    // 设置变更后调用：清空图池并按当前设置重新加载
    void applyAndReload();

    // ---- 供 GlassPanel 采样 ----
    QImage blurredWallpaper() const { return m_blurred; }
    // 把窗口坐标系下的矩形映射为壁纸图像坐标（cover 缩放裁切变换）
    QRectF coverSourceRect(const QImage& img, const QRect& targetRect) const;

signals:
    void wallpaperChanged();
    void statusMessage(const QString& msg);

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    void start();
    void requestOnline(int count);
    void scanLocal();
    bool loadNextLocal();
    void advance();
    void pushImage(const QImage& img, const QString& origin, bool silent = false);
    void beginFade();
    void rebuildBlurred();
    void notifyPanels();
    static QImage makeBlurred(const QImage& src);
    QString effectiveLocalDir() const;

    // 在线壁纸磁盘缓存：启动时先显示上次缓存的图，断网也能轮换
    QString cacheDir() const;
    void loadCachedWallpapers();
    void saveToCache(const QImage& img, const QString& origin);
    void pruneCache();

    QNetworkAccessManager* m_nam = nullptr;
    QTimer* m_timer = nullptr;
    QVariantAnimation* m_fade = nullptr;

    QVector<QImage> m_pool;
    QStringList m_localFiles;
    int m_localIndex = -1;
    int m_current = -1;
    int m_next = -1;
    QImage m_blurred;

    int m_mode = OnlineFirst;
    QString m_localDir;
    int m_intervalSec = 30;
    int m_endpointIdx = 0;
    int m_failStreak = 0;
    int m_pending = 0;
    bool m_started = false;
    QStringList m_endpoints;
    QString m_lastError;
};

} // namespace gc
