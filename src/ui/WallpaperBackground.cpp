#include "ui/WallpaperBackground.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QStandardPaths>
#include <QTimer>
#include <QVariantAnimation>

namespace gc {

namespace {

// 在线图源：优先返回 JPEG/PNG 的源。
// t.mwm.moe / loliapi 只返回 WebP，而官方 Qt 安装包不一定带 qwebp 插件，
// 因此只有在运行时可解码 WebP 时才启用，避免“下载成功但解不出来”的假失败。
QStringList onlineEndpoints()
{
    QStringList out;
    out << QStringLiteral("https://api.paugram.com/wallpaper/")
        << QStringLiteral("https://www.dmoe.cc/random.php")
        << QStringLiteral("https://img.xjh.me/random_img.php?type=bg&ctype=acg&return=302");

    const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    if (formats.contains(QByteArrayLiteral("webp"))) {
        out << QStringLiteral("https://t.mwm.moe/pc/")
            << QStringLiteral("https://www.loliapi.com/acg/pc/");
    }
    return out;
}

constexpr int kMaxPoolSize = 10;
constexpr int kMaxImageWidth = 2560;
constexpr int kKeepPrefetch = 3;

void drawCover(QPainter& p, const QImage& img, const QRect& target, qreal opacity)
{
    if (img.isNull() || target.isEmpty() || opacity <= 0.0)
        return;
    const double s = qMax(double(target.width()) / img.width(),
                          double(target.height()) / img.height());
    const QSizeF ds(img.width() * s, img.height() * s);
    const QPointF off(target.x() + (target.width() - ds.width()) / 2.0,
                      target.y() + (target.height() - ds.height()) / 2.0);
    p.save();
    p.setOpacity(opacity);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRectF(off, ds), img);
    p.restore();
}

} // namespace

WallpaperBackground::WallpaperBackground(QWidget* parent)
    : QWidget(parent)
{
    m_nam = new QNetworkAccessManager(this);
    m_timer = new QTimer(this);
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, &WallpaperBackground::advance);

    m_fade = new QVariantAnimation(this);
    m_fade->setDuration(900);
    m_fade->setStartValue(0.0);
    m_fade->setEndValue(1.0);
    m_fade->setEasingCurve(QEasingCurve::InOutQuad);
    connect(m_fade, &QVariantAnimation::valueChanged, this, [this](const QVariant&) {
        update(); // 重绘交叉淡化
    });
    connect(m_fade, &QVariantAnimation::finished, this, [this] {
        m_current = m_next;
        m_next = -1;
        rebuildBlurred();
        update();
        notifyPanels();
    });

    // 延迟启动，等窗口先显示出来
    QTimer::singleShot(0, this, &WallpaperBackground::start);
}

void WallpaperBackground::setIntervalSeconds(int sec)
{
    m_intervalSec = qBound(5, sec, 3600);
    if (m_timer->isActive())
        m_timer->start(m_intervalSec * 1000);
}

QString WallpaperBackground::effectiveLocalDir() const
{
    if (!m_localDir.isEmpty())
        return m_localDir;
    return QCoreApplication::applicationDirPath() + QStringLiteral("/wallpapers");
}

void WallpaperBackground::start()
{
    if (m_started)
        return;
    m_started = true;
    m_endpoints = onlineEndpoints();

    if (m_mode == Local) {
        scanLocal();
        loadNextLocal();
        return;
    }

    if (!QImageReader::supportedImageFormats().contains(QByteArrayLiteral("webp")))
        emit statusMessage(QStringLiteral("提示：当前 Qt 运行时不支持 WebP，已跳过 WebP 图源。"));

    // 先读磁盘缓存：断网或冷启动时也能立刻显示壁纸并继续轮换
    loadCachedWallpapers();
    requestOnline(4);
    if (m_mode == OnlineFirst) {
        // 同时准备本地列表，在线失败时可立即兜底
        scanLocal();
    }
}

void WallpaperBackground::applyAndReload()
{
    m_timer->stop();
    m_fade->stop();
    m_pool.clear();
    m_blurred = QImage();
    m_current = m_next = -1;
    m_localIndex = -1;
    m_failStreak = 0;
    m_started = false;
    update();
    start();
}

// ---------------------------------------------------------------- 磁盘缓存

QString WallpaperBackground::cacheDir() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty())
        base = QCoreApplication::applicationDirPath() + QStringLiteral("/cache");
    return base + QStringLiteral("/wallpapers");
}

void WallpaperBackground::loadCachedWallpapers()
{
    const QDir dir(cacheDir());
    if (!dir.exists())
        return;

    const QStringList files = dir.entryList(
        { QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.png") },
        QDir::Files, QDir::Time);
    int loaded = 0;
    for (const QString& f : files) {
        if (loaded >= 4)
            break;
        QImage img(dir.absoluteFilePath(f));
        if (img.isNull())
            continue;
        pushImage(img, dir.absoluteFilePath(f), true);
        ++loaded;
    }
    if (loaded > 0)
        emit statusMessage(QStringLiteral("已从本地缓存载入 %1 张壁纸，正在获取新图…").arg(loaded));
}

void WallpaperBackground::saveToCache(const QImage& img, const QString& origin)
{
    if (img.isNull() || img.width() < 320)
        return;
    QDir dir(cacheDir());
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return;

    const QByteArray hash = QCryptographicHash::hash(origin.toUtf8(), QCryptographicHash::Md5)
                                .toHex().left(12);
    const QString path = dir.filePath(QStringLiteral("wp-%1.jpg")
                                          .arg(QString::fromLatin1(hash)));
    if (QFileInfo::exists(path))
        return;

    QImage out = img;
    if (out.width() > 1920)
        out = out.scaledToWidth(1920, Qt::SmoothTransformation);
    if (out.save(path, "JPEG", 85))
        pruneCache();
}

void WallpaperBackground::pruneCache()
{
    constexpr int kMaxCacheFiles = 24;
    QDir dir(cacheDir());
    const QStringList files = dir.entryList({ QStringLiteral("wp-*.jpg") },
                                            QDir::Files, QDir::Time);
    for (int i = kMaxCacheFiles; i < files.size(); ++i)
        QFile::remove(dir.filePath(files[i]));
}

// ---------------------------------------------------------------- 在线拉取

void WallpaperBackground::requestOnline(int count)
{
    if (m_endpoints.isEmpty())
        return;
    for (int i = 0; i < count && m_pending < 6; ++i) {
        const QString url = m_endpoints.at(m_endpointIdx);
        m_endpointIdx = (m_endpointIdx + 1) % m_endpoints.size();

        QNetworkRequest req{ QUrl(url) };
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setRawHeader("User-Agent", "Mozilla/5.0 GameCreator/0.1");
        req.setTransferTimeout(15000);

        ++m_pending;
        QNetworkReply* reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            --m_pending;
            const QByteArray data = reply->readAll();
            const QImage img = QImage::fromData(data);
            if (reply->error() == QNetworkReply::NoError && !img.isNull()) {
                m_failStreak = 0;
                saveToCache(img, reply->url().toString());
                pushImage(img, reply->url().toString());
            } else {
                const QString contentType =
                    reply->header(QNetworkRequest::ContentTypeHeader).toString();
                if (reply->error() != QNetworkReply::NoError)
                    m_lastError = reply->errorString();
                else
                    m_lastError = QStringLiteral("无法解码 %1 图片（当前 Qt 可能缺少对应插件）")
                                      .arg(contentType.isEmpty() ? QStringLiteral("未知格式")
                                                                 : contentType);
                ++m_failStreak;
                // 已经缓存/加载了图片时不打扰用户，只有一张都没有才提示并回退本地
                if (m_failStreak == 3 && m_pool.isEmpty()) {
                    emit statusMessage(
                        QStringLiteral("在线壁纸暂不可用（%1），尝试本地图片文件夹。")
                            .arg(m_lastError));
                    if (m_mode != Local) {
                        scanLocal();
                        loadNextLocal();
                    }
                }
                // 图池不足时稍后重试
                if (m_pool.size() < 3)
                    QTimer::singleShot(8000, this, [this] {
                        if (m_pool.size() < 3 && m_pending < 2 && m_mode != Local)
                            requestOnline(1);
                    });
            }
            reply->deleteLater();
        });
    }
}

// ---------------------------------------------------------------- 本地图片

void WallpaperBackground::scanLocal()
{
    const QDir dir(effectiveLocalDir());
    const QStringList filters = { QStringLiteral("*.png"), QStringLiteral("*.jpg"),
                                  QStringLiteral("*.jpeg"), QStringLiteral("*.bmp"),
                                  QStringLiteral("*.webp") };
    m_localFiles = dir.entryList(filters, QDir::Files, QDir::Name);
    for (QString& f : m_localFiles)
        f = dir.absoluteFilePath(f);
    if (m_localFiles.isEmpty())
        emit statusMessage(QStringLiteral("本地壁纸文件夹为空：%1（可放入图片后在设置页重新应用）")
                               .arg(QDir::toNativeSeparators(dir.absolutePath())));
}

bool WallpaperBackground::loadNextLocal()
{
    if (m_localFiles.isEmpty())
        return false;
    m_localIndex = (m_localIndex + 1) % m_localFiles.size();
    const QImage img(m_localFiles[m_localIndex]);
    if (img.isNull())
        return false;
    pushImage(img, m_localFiles[m_localIndex]);
    return true;
}

// ---------------------------------------------------------------- 轮换

void WallpaperBackground::pushImage(const QImage& imgIn, const QString& origin, bool silent)
{
    Q_UNUSED(origin)
    QImage img = imgIn;
    if (img.width() > kMaxImageWidth)
        img = img.scaledToWidth(kMaxImageWidth, Qt::SmoothTransformation);
    m_pool.append(img);

    // 限制内存占用：淘汰最旧且不在展示中的
    while (m_pool.size() > kMaxPoolSize) {
        int victim = -1;
        for (int i = 0; i < m_pool.size(); ++i) {
            if (i != m_current && i != m_next) {
                victim = i;
                break;
            }
        }
        if (victim < 0)
            break;
        m_pool.remove(victim);
        if (m_current > victim) --m_current;
        if (m_next > victim) --m_next;
    }

    if (m_current < 0) {
        m_current = 0;
        rebuildBlurred();
        update();
        notifyPanels();
        if (!m_timer->isActive())
            m_timer->start(m_intervalSec * 1000);
        if (!silent)
            emit statusMessage(QStringLiteral("背景壁纸已加载，每 %1 秒轮换。").arg(m_intervalSec));
    }
}

void WallpaperBackground::advance()
{
    if (m_fade->state() == QVariantAnimation::Running)
        return;

    if (m_mode == Local) {
        loadNextLocal();
    } else if (m_pool.size() < 2) {
        if (m_mode != Local)
            requestOnline(2);
        if (m_mode == OnlineFirst && m_pool.size() < 2)
            loadNextLocal();
    }

    if (m_pool.size() < 2)
        return;
    m_next = (m_current + 1) % m_pool.size();
    beginFade();

    // 保持图池有富余
    if (m_mode != Local)
        requestOnline(kKeepPrefetch);
}

void WallpaperBackground::beginFade()
{
    if (m_next < 0 || m_next >= m_pool.size())
        return;
    rebuildBlurred(); // 提前准备下一张的模糊图，面板同步过渡
    m_fade->stop();
    m_fade->start();
}

// ---------------------------------------------------------------- 模糊缓存

QImage WallpaperBackground::makeBlurred(const QImage& src)
{
    if (src.isNull())
        return {};
    // 先缩小再模糊，大幅降低成本；毛玻璃不需要高频细节
    const int w = qMin(320, src.width());
    const QImage small = src.scaledToWidth(w, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QGraphicsScene scene;
    QGraphicsPixmapItem* item = scene.addPixmap(QPixmap::fromImage(small));
    QGraphicsBlurEffect* blur = new QGraphicsBlurEffect;
    blur->setBlurRadius(14);
    blur->setBlurHints(QGraphicsBlurEffect::QualityHint);
    item->setGraphicsEffect(blur);
    QImage out(small.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    scene.render(&p);
    p.end();
    // 放大回原尺寸，供 coverSourceRect 用同一套坐标换算
    return out.scaled(src.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

void WallpaperBackground::rebuildBlurred()
{
    const int idx = (m_fade->state() == QVariantAnimation::Running && m_next >= 0)
                        ? m_next
                        : m_current;
    if (idx >= 0 && idx < m_pool.size())
        m_blurred = makeBlurred(m_pool[idx]);
}

QRectF WallpaperBackground::coverSourceRect(const QImage& img, const QRect& targetRect) const
{
    if (img.isNull() || width() <= 0 || height() <= 0)
        return {};
    const double s = qMax(double(width()) / img.width(),
                          double(height()) / img.height());
    const double dw = img.width() * s;
    const double dh = img.height() * s;
    const double ox = (width() - dw) / 2.0;
    const double oy = (height() - dh) / 2.0;
    return QRectF((targetRect.x() - ox) / s, (targetRect.y() - oy) / s,
                  targetRect.width() / s, targetRect.height() / s);
}

void WallpaperBackground::notifyPanels()
{
    emit wallpaperChanged();
    // 面板是半透明采样，背景变化时需要重绘
    const QList<QWidget*> children = findChildren<QWidget*>();
    for (QWidget* w : children)
        w->update();
}

// ---------------------------------------------------------------- 绘制

void WallpaperBackground::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    if (m_current < 0 || m_current >= m_pool.size()) {
        // 无图时的简洁渐变底色
        QLinearGradient g(rect().topLeft(), rect().bottomRight());
        g.setColorAt(0, QColor(0x8e, 0x9e, 0xbf));
        g.setColorAt(1, QColor(0xc9, 0xb8, 0xd9));
        p.fillRect(rect(), g);
        return;
    }
    drawCover(p, m_pool[m_current], rect(), 1.0);
    if (m_fade->state() == QVariantAnimation::Running && m_next >= 0)
        drawCover(p, m_pool[m_next], rect(), m_fade->currentValue().toReal());
}

} // namespace gc
