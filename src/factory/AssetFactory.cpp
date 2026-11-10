#include "factory/AssetFactory.h"

#include <QColor>
#include <QPainter>
#include <QQueue>
#include <QSet>
#include <QStack>

#include <algorithm>
#include <climits>
#include <cmath>

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

struct ColorEntry {
    QRgb color = 0;
    int count = 0;
};

// 中位切分的一个颜色盒（按去重后的颜色及其像素频次组织，防止纯色块被重复空切）
struct ColorBox {
    QVector<ColorEntry> entries;
    qint64 totalPixels = 0;

    void recomputeCount()
    {
        totalPixels = 0;
        for (const ColorEntry& e : entries)
            totalPixels += e.count;
    }

    // 返回最大跨度通道 (0=R, 1=G, 2=B)，并通过 outSpan 返回该通道跨度 (0..255)
    int rangeChannel(int* outSpan = nullptr) const
    {
        if (entries.isEmpty()) {
            if (outSpan) *outSpan = 0;
            return -1;
        }
        int rmin = 255, rmax = 0, gmin = 255, gmax = 0, bmin = 255, bmax = 0;
        for (const ColorEntry& e : entries) {
            const QRgb c = e.color;
            rmin = qMin(rmin, int(qRed(c)));   rmax = qMax(rmax, int(qRed(c)));
            gmin = qMin(gmin, int(qGreen(c))); gmax = qMax(gmax, int(qGreen(c)));
            bmin = qMin(bmin, int(qBlue(c)));  bmax = qMax(bmax, int(qBlue(c)));
        }
        const int rr = rmax - rmin, gr = gmax - gmin, br = bmax - bmin;
        const int span = qMax(rr, qMax(gr, br));
        if (outSpan)
            *outSpan = span;
        if (span <= 0)
            return -1;
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

    // 若四边像素本来已是透明背景（如 gpt-image transparent 模式），直接返回避免误伤深色轮廓
    int borderTotal = 0, borderTransparent = 0;
    for (int x = 0; x < w; ++x) {
        borderTotal += 2;
        if (qAlpha(img.pixel(x, 0)) < 16) ++borderTransparent;
        if (qAlpha(img.pixel(x, h - 1)) < 16) ++borderTransparent;
    }
    for (int y = 1; y < h - 1; ++y) {
        borderTotal += 2;
        if (qAlpha(img.pixel(0, y)) < 16) ++borderTransparent;
        if (qAlpha(img.pixel(w - 1, y)) < 16) ++borderTransparent;
    }
    if (borderTotal > 0 && borderTransparent * 2 >= borderTotal)
        return img;

    // 键色：采样四角与四边中点共 8 个边缘锚点，在 RGB 空间取到其余样本距离之和最小的中心点（Medoid）
    const int m = qMax(1, qMin(w, h) / 64);
    const QPoint samplePts[8] = {
        QPoint(m, m), QPoint(w - 1 - m, m),
        QPoint(m, h - 1 - m), QPoint(w - 1 - m, h - 1 - m),
        QPoint(w / 2, m), QPoint(w / 2, h - 1 - m),
        QPoint(m, h / 2), QPoint(w - 1 - m, h / 2)
    };
    QVector<QRgb> samples;
    for (const QPoint& pt : samplePts) {
        const QRgb px = img.pixel(qBound(0, pt.x(), w - 1), qBound(0, pt.y(), h - 1));
        if (qAlpha(px) >= 16)
            samples.append(px);
    }
    if (samples.isEmpty())
        return img;

    QRgb key = samples.first();
    qint64 bestSumDist = LLONG_MAX;
    for (int i = 0; i < samples.size(); ++i) {
        qint64 sumDist = 0;
        for (int j = 0; j < samples.size(); ++j)
            sumDist += colorDist(samples[i], samples[j]);
        if (sumDist < bestSumDist) {
            bestSumDist = sumDist;
            key = samples[i];
        }
    }

    const int tol2 = tolerance * tolerance * 3;

    // 从四条边上的所有像素开始 BFS 泛洪
    QQueue<QPoint> queue;
    QVector<quint8> visited(size_t(w) * size_t(h), 0);
    auto enqueue = [&](int x, int y) {
        const size_t idx = size_t(y) * size_t(w) + size_t(x);
        if (visited[idx])
            return;
        const QRgb px = img.pixel(x, y);
        if (qAlpha(px) >= 16 && colorDist(px, key) > tol2)
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
    if (src.isNull() || targetSize < 1)
        return src;

    // 按长边等比缩放到 targetSize 内，避免瘦高角色/修长武器在 normalizeFrame 中触发二次最近邻抽点
    const int maxSide = qMax(src.width(), src.height());
    if (maxSide <= targetSize)
        return src;

    const double scale = double(targetSize) / double(maxSide);
    const int dw = qBound(1, qRound(src.width() * scale), targetSize);
    const int dh = qBound(1, qRound(src.height() * scale), targetSize);

    QImage out(dw, dh, QImage::Format_ARGB32);
    out.fill(Qt::transparent);

    auto luma = [](QRgb c) {
        return (qRed(c) * 299 + qGreen(c) * 587 + qBlue(c) * 114) / 1000;
    };

    for (int y = 0; y < dh; ++y) {
        const int y0 = (y * src.height()) / dh;
        const int y1 = qBound(y0 + 1, ((y + 1) * src.height()) / dh, src.height());
        for (int x = 0; x < dw; ++x) {
            const int x0 = (x * src.width()) / dw;
            const int x1 = qBound(x0 + 1, ((x + 1) * src.width()) / dw, src.width());

            // 统计块内不透明像素：5bit 量化桶做众数，同时记录最暗色（轮廓保护）
            QHash<quint32, int> counts;
            QHash<quint32, qint64> sumsR, sumsG, sumsB;
            int opaque = 0, total = 0;
            QRgb darkest = 0;
            int darkestLuma = INT_MAX;
            for (int by = y0; by < y1; ++by) {
                for (int bx = x0; bx < x1; ++bx) {
                    const QRgb px = src.pixel(bx, by);
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
    QHash<QRgb, int> hist;
    const int step = qMax(1, qMax(src.width(), src.height()) / 256); // 采样
    for (int y = 0; y < src.height(); y += step) {
        for (int x = 0; x < src.width(); x += step) {
            const QRgb px = src.pixel(x, y);
            if (qAlpha(px) > 128) {
                const QRgb opaqueRgb = qRgba(qRed(px), qGreen(px), qBlue(px), 255);
                ++hist[opaqueRgb];
            }
        }
    }
    if (hist.isEmpty())
        return { qRgba(255, 255, 255, 255) };

    maxColors = qBound(2, maxColors, 256);

    QVector<ColorEntry> uniqueEntries;
    uniqueEntries.reserve(hist.size());
    for (auto it = hist.constBegin(); it != hist.constEnd(); ++it)
        uniqueEntries.append(ColorEntry{ it.key(), it.value() });

    // 图像唯一色本就未超过上限时，直接原样保留全部颜色，避免无谓平均丢失点缀色
    if (uniqueEntries.size() <= maxColors) {
        std::sort(uniqueEntries.begin(), uniqueEntries.end(),
                  [](const ColorEntry& a, const ColorEntry& b) {
                      return a.count != b.count ? a.count > b.count : a.color < b.color;
                  });
        QVector<QRgb> exact;
        exact.reserve(uniqueEntries.size());
        for (const ColorEntry& e : uniqueEntries)
            exact.append(e.color);
        return exact;
    }

    QList<ColorBox> boxes;
    ColorBox first;
    first.entries = uniqueEntries;
    first.recomputeCount();
    boxes.append(first);

    while (boxes.size() < maxColors) {
        // 选择色域跨度与像素权重综合最高的非退化盒子切分（兼顾主色调过渡与高对比点缀色）
        int bestIdx = -1;
        double bestScore = -1.0;
        for (int i = 0; i < boxes.size(); ++i) {
            if (boxes[i].entries.size() < 2)
                continue;
            int span = 0;
            const int ch = boxes[i].rangeChannel(&span);
            if (ch < 0 || span <= 0)
                continue;
            const double score = double(span) * std::sqrt(double(qMax<qint64>(1, boxes[i].totalPixels)));
            if (score > bestScore) {
                bestScore = score;
                bestIdx = i;
            }
        }
        if (bestIdx < 0)
            break;

        ColorBox box = boxes.takeAt(bestIdx);
        const int ch = box.rangeChannel();
        std::sort(box.entries.begin(), box.entries.end(), [ch](const ColorEntry& a, const ColorEntry& b) {
            const int ca = ch == 0 ? qRed(a.color) : (ch == 1 ? qGreen(a.color) : qBlue(a.color));
            const int cb = ch == 0 ? qRed(b.color) : (ch == 1 ? qGreen(b.color) : qBlue(b.color));
            if (ca != cb)
                return ca < cb;
            return a.color < b.color;
        });

        // 按累计像素数的中位点切分，同时保证左右盒至少各含 1 个不同颜色
        qint64 half = box.totalPixels / 2;
        qint64 acc = 0;
        int splitIdx = 1;
        for (int i = 0; i < box.entries.size() - 1; ++i) {
            acc += box.entries[i].count;
            if (acc >= half) {
                splitIdx = i + 1;
                break;
            }
        }
        splitIdx = qBound(1, splitIdx, int(box.entries.size()) - 1);

        ColorBox left, right;
        left.entries = box.entries.mid(0, splitIdx);
        left.recomputeCount();
        right.entries = box.entries.mid(splitIdx);
        right.recomputeCount();
        boxes.append(left);
        boxes.append(right);
    }

    QVector<QRgb> palette;
    QSet<QRgb> seen;
    palette.reserve(boxes.size());
    for (const ColorBox& b : boxes) {
        if (b.entries.isEmpty() || b.totalPixels <= 0)
            continue;
        qint64 r = 0, g = 0, bl = 0;
        for (const ColorEntry& e : b.entries) {
            r  += qint64(qRed(e.color))   * e.count;
            g  += qint64(qGreen(e.color)) * e.count;
            bl += qint64(qBlue(e.color))  * e.count;
        }
        const QRgb avg = qRgba(int(r / b.totalPixels), int(g / b.totalPixels),
                               int(bl / b.totalPixels), 255);
        if (!seen.contains(avg)) {
            seen.insert(avg);
            palette.append(avg);
        }
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
    if (spec.type != AssetType::Scene) {
        img = cropToContent(img);
        img = downscaleBlock(img, qMax(1, spec.frameSize - 2));
    } else {
        img = downscaleBlock(img, spec.outputSize());
    }
    return buildPalette(img, spec.paletteSize);
}

QImage AssetFactory::quantize(const QImage& src, const QVector<QRgb>& palette, bool dither)
{
    if (palette.isEmpty())
        return src;
    QImage out = src.convertToFormat(QImage::Format_ARGB32);
    const int w = out.width(), h = out.height();

    // 精确 RGB 哈希缓存，避免 5-bit 粗粒度桶在边界色上的近似偏差
    QHash<QRgb, QRgb> cache;
    auto nearest = [&](QRgb c) -> QRgb {
        const QRgb rgbKey = c & 0x00FFFFFFu;
        auto it = cache.constFind(rgbKey);
        if (it != cache.constEnd())
            return it.value();
        int bestDist = INT_MAX;
        int bestIdx = 0;
        for (int i = 0; i < palette.size(); ++i) {
            const int d = colorDist(c, palette[i]);
            if (d < bestDist) {
                bestDist = d;
                bestIdx = i;
            }
        }
        const QRgb mapped = palette[bestIdx];
        cache.insert(rgbKey, mapped);
        return mapped;
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

    // 4. 按内框尺寸（frameSize - 2，预留四周至少 1px 透明边）一步降采样到位，
    //    避免在 normalizeFrame 中触发二次最近邻缩放而破坏轮廓
    img = downscaleBlock(img, qMax(1, spec.frameSize - 2));

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
