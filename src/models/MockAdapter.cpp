#include "models/MockAdapter.h"

#include <QBuffer>
#include <QColor>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>

namespace gc {
namespace {

// 32x32 方块网格：在 512 分辨率母版上以 16px 块绘制，降采样到 64 后每块 2px，像素感清晰。
constexpr int kGrid = 32;

struct BlockPainter {
    QImage* img;
    QPainter* p;
    int block; // 像素/块

    void rect(int bx, int by, int bw, int bh, const QColor& c) const
    {
        p->fillRect(QRect(bx * block, by * block, bw * block, bh * block), c);
    }
    // 带深色描边的块（模拟像素画轮廓）
    void rectOutlined(int bx, int by, int bw, int bh, const QColor& c, const QColor& outline) const
    {
        rect(bx, by, bw, bh, outline);
        rect(bx + 1, by + 1, bw - 2, bh - 2, c);
    }
};

QColor shifted(const QColor& base, int hueShift, int lightShift)
{
    QColor c = base;
    int h, s, l;
    c.getHsl(&h, &s, &l);
    c.setHsl((h + hueShift + 360) % 360,
             qBound(0, s, 255),
             qBound(0, l + lightShift, 255));
    return c;
}

} // namespace

MockAdapter::MockAdapter(QObject* parent)
    : IImageModel(parent)
{
}

QColor MockAdapter::referenceDominantColor(const QString& path) const
{
    QImage ref(path);
    if (ref.isNull())
        return QColor();
    // 取中央 50% 区域非背景像素的平均色作为“身份色”
    QImage sub = ref.copy(ref.width() / 4, ref.height() / 4, ref.width() / 2, ref.height() / 2)
                     .convertToFormat(QImage::Format_ARGB32);
    qint64 r = 0, g = 0, b = 0, n = 0;
    for (int y = 0; y < sub.height(); y += 4) {
        for (int x = 0; x < sub.width(); x += 4) {
            QRgb px = sub.pixel(x, y);
            QColor c(px);
            if (qAlpha(px) < 128)
                continue;
            // 排除常见纯背景灰
            if (c.value() > 235 && c.saturation() < 20)
                continue;
            r += qRed(px); g += qGreen(px); b += qBlue(px); ++n;
        }
    }
    if (n == 0)
        return QColor();
    return QColor(int(r / n), int(g / n), int(b / n));
}

void MockAdapter::generate(const GenerationRequest& req)
{
    const AssetSpec& spec = req.spec;

    // 身份基准种子：同一 prompt / 同一参考图 → 稳定的配色身份；候选索引只做轻微变化
    qlonglong baseSeed = 0;
    if (!spec.referenceImages.isEmpty())
        baseSeed = qHash(spec.referenceImages.first());
    else
        baseSeed = qHash(spec.prompt);
    qlonglong seed = baseSeed ^ (qlonglong(req.direction) * 7919) ^ (qlonglong(req.candidateIndex) * 104729);

    emit statusMessage(QStringLiteral("Mock 模型：生成 %1（方向 %2，候选 #%3）")
                           .arg(assetTypeToDisplay(spec.type),
                                directionToDisplay(req.direction))
                           .arg(req.candidateIndex + 1));

    QColor shirtRef;
    if (!spec.referenceImages.isEmpty())
        shirtRef = referenceDominantColor(spec.referenceImages.first());

    QImage img;
    switch (spec.type) {
    case AssetType::Character: img = drawCharacter(spec, req.direction, seed, shirtRef); break;
    case AssetType::Item:      img = drawItem(spec, seed); break;
    case AssetType::Scene:     img = drawScene(spec, seed); break;
    }

    if (img.isNull()) {
        emit requestFailed(QStringLiteral("Mock 模型生成失败：画布创建失败"));
        return;
    }

    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    if (!img.save(&buf, "PNG")) {
        emit requestFailed(QStringLiteral("Mock 模型生成失败：PNG 编码失败"));
        return;
    }

    emit imageReady(png, seed);
}

QImage MockAdapter::drawCharacter(const AssetSpec& spec, Direction dir, qlonglong seed,
                                  const QColor& shirtRef) const
{
    const int W = spec.modelWidth, H = spec.modelHeight;
    QImage img(W, H, QImage::Format_ARGB32);
    img.fill(QColor(200, 200, 200)); // 实心背景：用于验证「背景去除」环节

    QPainter pt(&img);
    pt.setRenderHint(QPainter::Antialiasing, false);
    BlockPainter bp{ &img, &pt, W / kGrid };

    QRandomGenerator rng(uint(seed & 0xffffffff));

    // 身份调色板：无参考图时由种子决定；有参考图（母版）时沿用母版主色 → 方向间身份一致
    QColor shirt = shirtRef.isValid()
                       ? shirtRef
                       : QColor::fromHsl(int(rng.bounded(360)), 180, 110);
    QColor hair   = shifted(shirt, 150, -20);
    QColor skin   = QColor(240, 200, 160);
    QColor pants  = shifted(shirt, 200, -70);
    QColor shoes  = QColor(70, 50, 40);
    QColor outline(40, 36, 44);
    QColor eye(30, 30, 40);

    // 候选间轻微差异：发色/上衣色相微调（模拟不同候选）
    if (shirtRef.isValid()) {
        shirt = shifted(shirt, int(rng.bounded(9)) - 4, int(rng.bounded(7)) - 3);
        hair  = shifted(hair,  int(rng.bounded(9)) - 4, int(rng.bounded(7)) - 3);
    }

    // ---- 头（含头发）----
    bp.rectOutlined(11, 6, 10, 9, skin, outline);          // 脸
    bp.rect(11, 5, 10, 3, hair);                            // 刘海
    bp.rect(10, 7, 1, 5, hair);                             // 侧发
    bp.rect(21, 7, 1, 5, hair);
    if (dir == Direction::Up) {
        bp.rect(11, 6, 10, 8, hair);                        // 背面：满头头发
        bp.rect(11, 13, 10, 2, hair);
    } else if (dir == Direction::Down) {
        bp.rect(13, 10, 2, 2, eye);                         // 正面：双眼
        bp.rect(17, 10, 2, 2, eye);
    } else if (dir == Direction::Left) {
        bp.rect(12, 10, 2, 2, eye);                         // 左侧面：单眼靠左
        bp.rect(11, 7, 2, 5, hair);                         // 遮住半边脸
    } else { // Right
        bp.rect(18, 10, 2, 2, eye);
        bp.rect(19, 7, 2, 5, hair);
    }

    // ---- 躯干 ----
    bp.rectOutlined(12, 15, 8, 8, shirt, outline);
    bp.rect(13, 16, 6, 1, shifted(shirt, 0, 25));           // 高光
    // 腰带
    bp.rect(12, 21, 8, 1, shifted(pants, 0, 10));

    // ---- 手臂 ----
    bp.rectOutlined(10, 16, 2, 6, shirt, outline);
    bp.rectOutlined(20, 16, 2, 6, shirt, outline);
    bp.rect(10, 21, 2, 1, skin);
    bp.rect(20, 21, 2, 1, skin);

    // ---- 腿与鞋（脚底 = 内容底部，验证 bottom-center 锚点）----
    bp.rectOutlined(13, 23, 2, 4, pants, outline);
    bp.rectOutlined(17, 23, 2, 4, pants, outline);
    bp.rect(13, 27, 2, 1, shoes);
    bp.rect(17, 27, 2, 1, shoes);

    // ---- 随机小配饰（种子决定，同一身份稳定）----
    if (rng.bounded(3) == 0) {
        bp.rect(16, 15, 1, 1, QColor(255, 215, 80));        // 胸口徽章
    }
    if (rng.bounded(4) == 0 && dir != Direction::Up) {
        bp.rect(12, 14, 8, 1, shifted(shirt, 0, -35));      // 领口
    }

    pt.end();
    return img;
}

QImage MockAdapter::drawItem(const AssetSpec& spec, qlonglong seed) const
{
    const int W = spec.modelWidth, H = spec.modelHeight;
    QImage img(W, H, QImage::Format_ARGB32);
    img.fill(QColor(200, 200, 200));

    QPainter pt(&img);
    pt.setRenderHint(QPainter::Antialiasing, false);
    BlockPainter bp{ &img, &pt, W / kGrid };

    QRandomGenerator rng(uint(seed & 0xffffffff));
    QColor outline(40, 36, 44);
    QColor gold(255, 205, 70);
    QColor wood(140, 95, 55);
    QColor liquid = QColor::fromHsl(int(rng.bounded(360)), 200, 120);

    const QString p = spec.prompt.toLower();
    const bool wantSword  = p.contains(QStringLiteral("剑")) || p.contains(QStringLiteral("刀"))
                            || p.contains(QLatin1String("sword")) || p.contains(QLatin1String("blade"));
    const bool wantPotion = p.contains(QStringLiteral("药")) || p.contains(QStringLiteral("水"))
                            || p.contains(QLatin1String("potion"));
    const bool wantChest  = p.contains(QStringLiteral("箱")) || p.contains(QLatin1String("chest"))
                            || p.contains(QLatin1String("box"));

    if (wantSword) {
        // 剑刃
        QColor blade(210, 215, 225);
        bp.rectOutlined(15, 4, 2, 17, blade, outline);
        bp.rect(15, 5, 1, 15, QColor(240, 244, 250));       // 刃高光
        bp.rect(14, 21, 4, 2, gold);                        // 护手
        bp.rectOutlined(15, 23, 2, 4, wood, outline);       // 剑柄
        bp.rect(15, 27, 2, 1, gold);                        // 柄首
    } else if (wantPotion) {
        bp.rect(15, 6, 2, 2, wood);                         // 软木塞
        bp.rectOutlined(15, 8, 2, 4, QColor(190, 225, 235), outline);  // 瓶颈
        bp.rectOutlined(13, 12, 6, 10, QColor(190, 225, 235), outline);// 瓶身
        bp.rect(14, 16, 4, 5, liquid);                      // 药液
        bp.rect(14, 13, 1, 3, QColor(240, 250, 255));       // 玻璃高光
    } else if (wantChest) {
        bp.rectOutlined(9, 14, 14, 10, wood, outline);      // 箱体
        bp.rect(9, 17, 14, 1, shifted(wood, 0, -30));       // 盖沿
        bp.rect(15, 18, 2, 3, gold);                        // 锁
        bp.rect(11, 15, 1, 8, shifted(wood, 0, 25));        // 木纹高光
        bp.rect(20, 15, 1, 8, shifted(wood, 0, -20));
    } else {
        // 默认：宝石
        QColor gem = QColor::fromHsl(int(rng.bounded(360)), 220, 130);
        for (int r = 0; r < 7; ++r) {
            int w = (r < 3) ? (r + 1) * 2 : (7 - r) * 2;    // 菱形
            bp.rectOutlined(16 - w / 2, 11 + r, qMax(w, 1), 1, gem, outline);
        }
        bp.rect(15, 13, 1, 2, shifted(gem, 0, 60));
    }

    pt.end();
    return img;
}

QImage MockAdapter::drawScene(const AssetSpec& spec, qlonglong seed) const
{
    const int W = spec.modelWidth, H = spec.modelHeight;
    QImage img(W, H, QImage::Format_ARGB32);

    QRandomGenerator rng(uint(seed & 0xffffffff));

    // 天空渐变
    QLinearGradient sky(0, 0, 0, H * 0.62);
    sky.setColorAt(0.0, QColor(120, 170, 235));
    sky.setColorAt(1.0, QColor(205, 230, 250));
    QPainter pt(&img);
    pt.fillRect(QRect(0, 0, W, int(H * 0.62)), sky);

    // 太阳
    pt.setBrush(QColor(255, 225, 140));
    pt.setPen(Qt::NoPen);
    pt.drawEllipse(QPoint(W * 3 / 4, H / 6), W / 14, W / 14);

    // 远山（三角）
    pt.setBrush(QColor(110, 125, 150));
    for (int i = 0; i < 4; ++i) {
        int cx = (i * W / 3 + int(rng.bounded(W / 6))) % W;
        int peak = int(H * (0.30 + 0.05 * rng.bounded(4.0)));
        QPolygon tri;
        tri << QPoint(cx - W / 5, int(H * 0.62))
            << QPoint(cx, peak)
            << QPoint(cx + W / 5, int(H * 0.62));
        pt.drawPolygon(tri);
    }

    // 地面
    pt.fillRect(QRect(0, int(H * 0.62), W, H - int(H * 0.62)), QColor(95, 165, 90));
    pt.setBrush(QColor(70, 140, 70));
    for (int i = 0; i < 160; ++i) {
        pt.drawRect(QRect(int(rng.bounded(W)), int(H * 0.62 + rng.bounded(H * 0.36)), 4, 4));
    }
    // 小树
    pt.setBrush(QColor(60, 100, 60));
    for (int i = 0; i < 6; ++i) {
        int x = int(rng.bounded(W));
        int y = int(H * 0.62 + rng.bounded(H * 0.18));
        pt.drawEllipse(QPoint(x, y), 14, 18);
        pt.fillRect(QRect(x - 2, y, 4, 16), QColor(100, 70, 45));
    }
    // 小路
    QPainterPath road;
    road.moveTo(W * 0.42, H * 0.62);
    road.quadTo(W * 0.5, H * 0.8, W * 0.34, H);
    road.lineTo(W * 0.62, H);
    road.quadTo(W * 0.56, H * 0.8, W * 0.52, H * 0.62);
    pt.fillPath(road, QColor(200, 180, 140));

    pt.end();
    return img;
}

} // namespace gc
