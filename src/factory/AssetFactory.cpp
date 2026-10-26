#include "factory/AssetFactory.h"

#include <QColor>
#include <QPainter>
#include <QQueue>
#include <QSet>
#include <QStack>

#include <algorithm>
#include <climits>

namespace gc {
namespace {

inline int colorDist(QRgb a, QRgb b)
{
    const int dr = int(qRed(a))   - int(qRed(b));
    const int dg = int(qGreen(a)) - int(qGreen(b));
    const int db = int(qBlue(a))  - int(qBlue(b));
    return dr * dr + dg * dg + db * db;
}

// 场景概念图固定输出为正方形：只做居中的裁切/补边，不做二次缩放，
// 避免破坏已经降采样好的像素网格。
QImage fitToSquare(const QImage& src, int size)
{
    if (src.width() == size && src.height() == size)
        return src;
    QImage out(size, size, QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage((size - src.width()) / 2, (size - src.height()) / 2, src);
    p.end();
    return out;
}

// 中位切分的一个颜色盒
struct ColorBox {
    QVector<QRgb> pixels;
    int rangeChannel() const
    {
        if (pixels.isEmpty())
            return -1;
        int rmin = 255, rmax = 0, gmin = 255, gmax = 0, bmin = 255, bmax = 0;
        for (QRgb c : pixels) {
            rmin = qMin(rmin, int(qRed(c)));   rmax = qMax(rmax, int(qRed(c)));
            gmin = qMin(gmin, int(qGreen(c))); gmax = qMax(gmax, int(qGreen(c)));
            bmin = qMin(bmin, int(qBlue(c)));  bmax = qMax(bmax, int(qBlue(c)));
        }
        const int rr = rmax - rmin, gr = gmax - gmin, br = bmax - bmin;
        if (rr >= gr && rr >= br) return 0;
        if (gr >= br) return 1;
        return 2;
    }
};

} // namespace

QImage AssetFactory::removeBackground(const QImage& src0, int tolerance)
{
    QImage img = src0;
    if (img.format() != QImage::Format_ARGB32 && img.format() != QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(QImage::Format_ARGB32);
    if (img.format() == QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(QImage::Format_ARGB32);
    if (img.isNull())
        return img;

    const int w = img.width(), h = img.height();

    // 键色：四角颜色中位数（抗单角反锯齿干扰）
    QVector<QRgb> corners;
    const int m = qMax(1, qMin(w, h) / 64); // 采样角部小区域
    for (const QPoint& pt : { QPoint(0, 0), QPoint(w - m, 0), QPoint(0, h - m), QPoint(w - m, h - m) }) {
        corners.append(img.pixel(qBound(0, pt.x(), w - 1), qBound(0, pt.y(), h - 1)));
    }
    std::sort(corners.begin(), corners.end());
    const QRgb key = corners[corners.size() / 2];

    const int tol2 = tolerance * tolerance * 3;

    // 从四条边上的所有像素开始 BFS 泛洪
    QQueue<QPoint> queue;
    QVector<quint8> visited(size_t(w) * size_t(h), 0);
    auto enqueue = [&](int x, int y) {
        const size_t idx = size_t(y) * size_t(w) + size_t(x);
        if (visited[idx])
            return;
        if (colorDist(img.pixel(x, y), key) > tol2)
            return;
        visited[idx] = 1;
        queue.enqueue(QPoint(x, y));
    };
    for (int x = 0; x < w; ++x) { enqueue(x, 0); enqueue(x, h - 1); }
    for (int y = 0; y < h; ++y) { enqueue(0, y); enqueue(w - 1, y); }

    QImage out = img;
    while (!queue.isEmpty()) {
        const QPoint pt = queue.dequeue();
        out.setPixel(pt.x(), pt.y(), qRgba(0, 0, 0, 0));
        const int x = pt.x(), y = pt.y();
        if (x > 0)      enqueue(x - 1, y);
        if (x < w - 1)  enqueue(x + 1, y);
        if (y > 0)      enqueue(x, y - 1);
        if (y < h - 1)  enqueue(x, y + 1);
    }
    return out;
}

QRect AssetFactory::contentBounds(const QImage& src, int alphaThreshold)
{
    const int w = src.width(), h = src.height();
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (qAlpha(src.pixel(x, y)) > alphaThreshold) {
                if (x < x0) x0 = x;
                if (y < y0) y0 = y;
                if (x > x1) x1 = x;
                if (y > y1) y1 = y;
            }
        }
    }
    if (x1 < x0)
        return QRect();
    return QRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}

QImage AssetFactory::cropToContent(const QImage& src)
{
    const QRect r = contentBounds(src);
    if (r.isNull())
        return src;
    return src.copy(r);
}

QImage AssetFactory::downscaleBlock(const QImage& src, int targetSize)
{
    if (src.width() <= targetSize || src.height() <= targetSize)
        return src;

    // 整数块因子：512→64 取 8；结果尺寸不小于 target 的一半
    const int factor = qMax(2, qRound(double(src.width()) / targetSize));
    const int dw = src.width() / factor;
    const int dh = src.height() / factor;
    if (dw < 1 || dh < 1)
        return src;

    QImage out(dw, dh, QImage::Format_ARGB32);
    out.fill(Qt::transparent);

    auto luma = [](QRgb c) {
        return (qRed(c) * 299 + qGreen(c) * 587 + qBlue(c) * 114) / 1000;
    };

    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            // 统计块内不透明像素：5bit 量化桶做众数，同时记录最暗色（轮廓保护）
            QHash<quint32, int> counts;
            QHash<quint32, qint64> sumsR, sumsG, sumsB;
            int opaque = 0, total = 0;
            QRgb darkest = 0;
            int darkestLuma = INT_MAX;
            for (int by = 0; by < factor; ++by) {
                for (int bx = 0; bx < factor; ++bx) {
                    const QRgb px = src.pixel(x * factor + bx, y * factor + by);
                    ++total;
                    if (qAlpha(px) < 8)
                        continue;
                    ++opaque;
                    const quint32 key = (quint32(qRed(px) >> 3) << 10)
                                      | (quint32(qGreen(px) >> 3) << 5)
                                      | quint32(qBlue(px) >> 3);
                    ++counts[key];
                    sumsR[key] += qRed(px);
                    sumsG[key] += qGreen(px);
                    sumsB[key] += qBlue(px);
                    const int l = luma(px);
                    if (l < darkestLuma) {
                        darkestLuma = l;
                        darkest = px;
                    }
                }
            }
            if (opaque == 0)
                continue;

            if (opaque == total) {
                // 内部块：取众数桶的平均色（去掉模型抗锯齿产生的中间色）
                quint32 bestKey = 0;
                int bestCount = -1;
                for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
                    if (it.value() > bestCount) {
                        bestCount = it.value();
                        bestKey = it.key();
                    }
                }
                const int n = bestCount;
                out.setPixel(x, y, qRgba(int(sumsR[bestKey] / n), int(sumsG[bestKey] / n),
                                         int(sumsB[bestKey] / n), 255));
            } else {
                // 边界块：保留最暗不透明色，防止细轮廓被缩放抹掉
                out.setPixel(x, y, qRgba(qRed(darkest), qGreen(darkest),
                                         qBlue(darkest), 255));
            }
        }
    }
    return out;
}

QImage AssetFactory::upscaleNearest(const QImage& src, int factor)
{
    if (factor <= 1 || src.isNull())
        return src;
    return src.scaled(src.width() * factor, src.height() * factor,
                      Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

QVector<QRgb> AssetFactory::buildPalette(const QImage& src, int maxColors)
{
    QVector<QRgb> pixels;
    const int step = qMax(1, qMax(src.width(), src.height()) / 256); // 采样
    for (int y = 0; y < src.height(); y += step)
        for (int x = 0; x < src.width(); x += step) {
            const QRgb px = src.pixel(x, y);
            if (qAlpha(px) > 128)
                pixels.append(px);
        }
    if (pixels.isEmpty())
        return { qRgba(255, 255, 255, 255) };

    maxColors = qBound(2, maxColors, 256);

    QList<ColorBox> boxes;
    ColorBox first;
    first.pixels = pixels;
    boxes.append(first);

    while (boxes.size() < maxColors) {
        // 找像素最多且有色域范围的盒子切分
        int bestIdx = -1;
        int bestRange = 0;
        int bestCount = 0;
        for (int i = 0; i < boxes.size(); ++i) {
            const int range = boxes[i].rangeChannel() >= 0 ? 1 : 0;
            const int ch = boxes[i].rangeChannel();
            Q_UNUSED(ch);
            if (boxes[i].pixels.size() < 2)
                continue;
            // 简化策略：优先切像素最多的盒子
            if (range > 0 && boxes[i].pixels.size() > bestCount) {
                bestCount = boxes[i].pixels.size();
                bestIdx = i;
            }
        }
        if (bestIdx < 0)
            break;

        ColorBox box = boxes.takeAt(bestIdx);
        const int ch = box.rangeChannel();
        std::sort(box.pixels.begin(), box.pixels.end(), [ch](QRgb a, QRgb b) {
            const int ca = ch == 0 ? qRed(a) : (ch == 1 ? qGreen(a) : qBlue(a));
            const int cb = ch == 0 ? qRed(b) : (ch == 1 ? qGreen(b) : qBlue(b));
            return ca < cb;
        });
        const int mid = box.pixels.size() / 2;
        ColorBox left, right;
        left.pixels = box.pixels.mid(0, mid);
        right.pixels = box.pixels.mid(mid);
        boxes.append(left);
        boxes.append(right);
    }

    QVector<QRgb> palette;
    palette.reserve(boxes.size());
    for (const ColorBox& b : boxes) {
        if (b.pixels.isEmpty())
            continue;
        qint64 r = 0, g = 0, bl = 0;
        for (QRgb c : b.pixels) {
            r += qRed(c); g += qGreen(c); bl += qBlue(c);
        }
        palette.append(qRgba(int(r / b.pixels.size()), int(g / b.pixels.size()),
                             int(bl / b.pixels.size()), 255));
    }
    if (palette.isEmpty())
        palette.append(qRgba(255, 255, 255, 255));
    return palette;
}

QVector<QRgb> AssetFactory::paletteFromRaw(const QImage& raw, const AssetSpec& spec)
{
    if (raw.isNull())
        return {};
    QImage img = raw.convertToFormat(QImage::Format_ARGB32);
    if (spec.type != AssetType::Scene && !spec.sourceHasAlpha)
        img = removeBackground(img);
    img = cropToContent(img);
    img = downscaleBlock(img, spec.outputSize());
    return buildPalette(img, spec.paletteSize);
}

QImage AssetFactory::quantize(const QImage& src, const QVector<QRgb>& palette, bool dither)
{
    if (palette.isEmpty())
        return src;
    QImage out = src.convertToFormat(QImage::Format_ARGB32);
    const int w = out.width(), h = out.height();

    // 预建 32x32x32 RGB 立方体加速最近色查找
    QVector<qint16> cache(32 * 32 * 32, -1);
    auto nearest = [&](QRgb c) -> QRgb {
        const int key = ((qRed(c) >> 3) << 10) | ((qGreen(c) >> 3) << 5) | (qBlue(c) >> 3);
        if (cache[key] >= 0)
            return palette[cache[key]];
        int bestDist = INT_MAX;
        int bestIdx = 0;
        for (int i = 0; i < palette.size(); ++i) {
            const int d = colorDist(c, palette[i]);
            if (d < bestDist) {
                bestDist = d;
                bestIdx = i;
            }
        }
        cache[key] = qint16(bestIdx);
        return palette[bestIdx];
    };

    if (!dither) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const QRgb px = out.pixel(x, y);
                if (qAlpha(px) < 128) {
                    out.setPixel(x, y, qRgba(0, 0, 0, 0));
                    continue;
                }
                out.setPixel(x, y, qRgba(qRed(nearest(px)), qGreen(nearest(px)),
                                         qBlue(nearest(px)), qAlpha(px)));
            }
        return out;
    }

    // Floyd–Steinberg 抖动
    QVector<QVector<int>> errR(h, QVector<int>(w, 0));
    QVector<QVector<int>> errG(h, QVector<int>(w, 0));
    QVector<QVector<int>> errB(h, QVector<int>(w, 0));
    auto diffuse = [&](int x, int y, double f, int er, int eg, int eb) {
        if (x < 0 || x >= w || y < 0 || y >= h)
            return;
        errR[y][x] += int(f * er);
        errG[y][x] += int(f * eg);
        errB[y][x] += int(f * eb);
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const QRgb px = out.pixel(x, y);
            if (qAlpha(px) < 128) {
                out.setPixel(x, y, qRgba(0, 0, 0, 0));
                continue;
            }
            const QRgb shifted = qRgba(qBound(0, int(qRed(px)) + errR[y][x], 255),
                                       qBound(0, int(qGreen(px)) + errG[y][x], 255),
                                       qBound(0, int(qBlue(px)) + errB[y][x], 255), 255);
            const QRgb mapped = nearest(shifted);
            out.setPixel(x, y, qRgba(qRed(mapped), qGreen(mapped), qBlue(mapped), qAlpha(px)));
            const int er = int(qRed(shifted)) - int(qRed(mapped));
            const int eg = int(qGreen(shifted)) - int(qGreen(mapped));
            const int eb = int(qBlue(shifted)) - int(qBlue(mapped));
            diffuse(x + 1, y, 7.0 / 16, er, eg, eb);
            diffuse(x - 1, y + 1, 3.0 / 16, er, eg, eb);
            diffuse(x, y + 1, 5.0 / 16, er, eg, eb);
            diffuse(x + 1, y + 1, 1.0 / 16, er, eg, eb);
        }
    }
    return out;
}

int AssetFactory::colorCount(const QImage& src, int alphaThreshold)
{
    QSet<QRgb> colors;
    for (int y = 0; y < src.height(); ++y)
        for (int x = 0; x < src.width(); ++x) {
            const QRgb px = src.pixel(x, y);
            if (qAlpha(px) > alphaThreshold)
                colors.insert(qRgb(qRed(px), qGreen(px), qBlue(px)));
        }
    return colors.size();
}

QImage AssetFactory::normalizeFrame(const QImage& content, int frameSize)
{
    QImage frame(frameSize, frameSize, QImage::Format_ARGB32);
    frame.fill(Qt::transparent);

    QImage c = content;
    // 缩放到 (frameSize-2) 内，保证四周至少 1px 透明边（避免裁切与贴边告警）
    const int inner = frameSize - 2;
    if (c.width() > inner || c.height() > inner)
        c = c.scaled(inner, inner, Qt::KeepAspectRatio, Qt::FastTransformation);

    // 脚底锚点：底边留 1px 呼吸边，水平居中
    const int x = (frameSize - c.width()) / 2;
    const int y = frameSize - c.height() - 1;
    QPainter p(&frame);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(qMax(0, x), qMax(0, y), c);
    p.end();
    return frame;
}

StandardizeResult AssetFactory::standardize(const QImage& raw, const AssetSpec& spec,
                                            const QVector<QRgb>& sharedPalette)
{
    StandardizeResult result;
    if (raw.isNull())
        return result;

    // 1. 背景去除（场景概念图保留完整画面，不做抠除）
    QImage img = raw.convertToFormat(QImage::Format_ARGB32);
    if (spec.type != AssetType::Scene && !spec.sourceHasAlpha)
        img = removeBackground(img);

    // 2. 内容包围盒
    result.contentRect = contentBounds(img);

    if (spec.type == AssetType::Scene) {
        // 场景：整图像素化到 2 倍目标尺寸并量化，不做锚点对齐
        img = downscaleBlock(img, spec.outputSize());
        img = fitToSquare(img, spec.outputSize());
        QVector<QRgb> pal = sharedPalette.isEmpty()
                                ? buildPalette(img, qMax(spec.paletteSize, 48))
                                : sharedPalette;
        img = quantize(img, pal, spec.useDither);
        result.palette = pal;
        result.frame = img;
        result.preview = upscaleNearest(img, 2);
        return result;
    }

    // 3. 裁切到内容
    img = cropToContent(img);

    // 4. 整数倍降采样到目标像素网格
    img = downscaleBlock(img, spec.frameSize);

    // 5. 调色板量化（角色多方向共用母版色板，防止方向间色彩漂移）
    QVector<QRgb> pal = sharedPalette.isEmpty()
                            ? buildPalette(img, spec.paletteSize)
                            : sharedPalette;
    img = quantize(img, pal, spec.useDither);
    result.palette = pal;

    // 6. 脚底锚点标准化帧
    result.frame = normalizeFrame(img, spec.frameSize);

    // 7. 预览放大
    result.preview = upscaleNearest(result.frame, qMax(1, 128 / spec.frameSize));
    if (result.preview.width() < 128)
        result.preview = upscaleNearest(result.frame, 4);
    return result;
}

QImage AssetFactory::packSheet(const QVector<QVector<QImage>>& rows, int frameSize)
{
    int maxCols = 1;
    for (const QVector<QImage>& r : rows)
        maxCols = qMax(maxCols, r.size());
    QImage sheet(maxCols * frameSize, rows.size() * frameSize, QImage::Format_ARGB32);
    sheet.fill(Qt::transparent);
    QPainter p(&sheet);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (int row = 0; row < rows.size(); ++row) {
        for (int col = 0; col < rows[row].size(); ++col) {
            p.drawImage(col * frameSize, row * frameSize, rows[row][col]);
        }
    }
    p.end();
    return sheet;
}

QJsonObject AssetFactory::sheetMetadata(const AssetSpec& spec,
                                        const QVector<QVector<Direction>>& rowDirs,
                                        int frameSize,
                                        const QVector<QRgb>& palette)
{
    QJsonObject root;
    root.insert(QStringLiteral("frameWidth"), frameSize);
    root.insert(QStringLiteral("frameHeight"), frameSize);
    root.insert(QStringLiteral("fps"), 8); // MVP：待机单帧，fps 仅作占位
    root.insert(QStringLiteral("anchor"), spec.anchor);
    root.insert(QStringLiteral("assetType"), assetTypeToString(spec.type));

    QJsonArray rowsArr;
    for (const QVector<Direction>& row : rowDirs) {
        QJsonArray dirArr;
        for (Direction d : row)
            dirArr.append(directionToString(d));
        QJsonObject rowObj;
        rowObj.insert(QStringLiteral("directions"), dirArr);
        rowObj.insert(QStringLiteral("frames"), row.size());
        rowsArr.append(rowObj);
    }
    root.insert(QStringLiteral("rows"), rowsArr);

    QJsonArray palArr;
    for (QRgb c : palette)
        palArr.append(QStringLiteral("#%1").arg(QString::number(c & 0xFFFFFF, 16).rightJustified(6, QLatin1Char('0')).toUpper()));
    root.insert(QStringLiteral("palette"), palArr);
    return root;
}

} // namespace gc
