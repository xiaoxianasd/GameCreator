#pragma once

// VisualHeuristics：视觉质量启发式审查（技术路线 7.3 的无模型近似实现）。
// 在没有独立视觉模型的条件下，先用可计算的启发式指标拦截明显低质量结果
// （简笔/方块人、无轮廓、无层次、方向间色彩漂移、身高不一致）。
// ReviewReport.stage = "vision-heuristic"；接入真实视觉模型后可与其报告合并。

#include "core/AssetSpec.h"
#include "core/ReviewReport.h"

#include <QImage>
#include <QSet>
#include <QString>
#include <QVector>

#include <algorithm>
#include <climits>
#include <cmath>
#include <utility>

namespace gc {

class VisualHeuristics {
public:
    struct FrameEntry {
        Direction dir = Direction::Down;
        QImage image;
    };

    static ReviewReport run(const AssetSpec& spec, const QVector<FrameEntry>& frames)
    {
        ReviewReport report;
        report.versionId = spec.id;
        report.stage = QStringLiteral("vision-heuristic");
        report.created = QDateTime::currentDateTime();

        double weightTotal = 0.0;
        double weightPass = 0.0;

        QVector<QSet<QRgb>> palettes;
        QVector<int> contentHeights;

        for (const FrameEntry& f : frames) {
            const QString dirName = spec.type == AssetType::Character
                                        ? directionToDisplay(f.dir) : QStringLiteral("单帧");
            if (f.image.isNull())
                continue;

            // ---- 1. 轮廓覆盖率：内容边界像素中深色轮廓的占比 ----
            {
                const auto [boundary, darkBoundary] = boundaryStats(f.image);
                weightTotal += 2.0;
                if (boundary > 0 && double(darkBoundary) / boundary >= 0.25) {
                    weightPass += 2.0;
                } else {
                    addIssue(report, QStringLiteral("weak-outline"), Severity::Warning,
                             QStringLiteral("%1：轮廓线覆盖不足（边界深色像素 %2/%3），"
                                            "小尺寸下轮廓可能不清晰")
                                 .arg(dirName).arg(darkBoundary).arg(boundary));
                }
            }

            // ---- 2. 明暗层次：亮度分层是否足够（高光/中间调/阴影） ----
            {
                const int levels = lumaLevels(f.image);
                weightTotal += 2.0;
                if (levels >= 3) {
                    weightPass += 2.0;
                } else {
                    addIssue(report, QStringLiteral("flat-shading"), Severity::Warning,
                             QStringLiteral("%1：明暗层次不足（仅 %2 个亮度级，建议至少 3 级："
                                            "高光/中间调/阴影）").arg(dirName).arg(levels));
                }
            }

            // ---- 3. 细节丰富度：内容颜色数 ----
            {
                const int colors = contentColors(f.image);
                weightTotal += 1.0;
                if (colors >= 8) {
                    weightPass += 1.0;
                } else {
                    addIssue(report, QStringLiteral("low-detail"), Severity::Warning,
                             QStringLiteral("%1：细节颜色过少（%2 色），疑似简笔或结构过于简单")
                                 .arg(dirName).arg(colors));
                }
            }

            palettes.append(paletteOf(f.image));
            contentHeights.append(contentHeight(f.image));
        }

        // ---- 4. 方向间色彩漂移（身份一致性近似）----
        if (palettes.size() >= 2) {
            weightTotal += 3.0;
            double maxDrift = 0.0;
            for (int i = 0; i < palettes.size(); ++i) {
                for (int j = i + 1; j < palettes.size(); ++j) {
                    const double drift = paletteDrift(palettes[i], palettes[j]);
                    maxDrift = std::max(maxDrift, drift);
                }
            }
            if (maxDrift <= 70.0) {
                weightPass += 3.0;
            } else {
                addIssue(report, QStringLiteral("color-drift"), Severity::Warning,
                         QStringLiteral("方向间色彩漂移（平均最近色距离 %1，阈值 70）："
                                        "建议统一使用母版色板重新标准化")
                             .arg(QString::number(maxDrift, 'f', 1)));
            }
        }

        // ---- 5. 身高/体积一致性（角色）----
        if (spec.type == AssetType::Character && contentHeights.size() >= 2) {
            weightTotal += 2.0;
            const auto [hmin, hmax] = std::minmax_element(contentHeights.begin(),
                                                          contentHeights.end());
            if (*hmax <= 0 || double(*hmin) / *hmax >= 0.8) {
                weightPass += 2.0;
            } else {
                addIssue(report, QStringLiteral("volume-mismatch"), Severity::Warning,
                         QStringLiteral("各方向身高/体积不一致（%1~%2px）：动画播放时会产生画布跳动")
                             .arg(*hmin).arg(*hmax));
            }
        }

        report.score = weightTotal > 0 ? weightPass / weightTotal : 0.0;
        report.passed = !hasError(report) && report.score >= 0.7;
        return report;
    }

private:
    enum class Severity { Error, Warning };

    static void addIssue(ReviewReport& r, const QString& id, Severity s, const QString& msg)
    {
        ReviewIssue issue;
        issue.id = id;
        issue.severity = s == Severity::Error ? QStringLiteral("error") : QStringLiteral("warning");
        issue.message = msg;
        r.issues.append(issue);
    }

    static bool hasError(const ReviewReport& r)
    {
        return std::any_of(r.issues.begin(), r.issues.end(),
                           [](const ReviewIssue& i) { return i.severity == QLatin1String("error"); });
    }

    static int lumaOf(QRgb c)
    {
        return (qRed(c) * 299 + qGreen(c) * 587 + qBlue(c) * 114) / 1000;
    }

    // 返回 {边界不透明像素数, 其中深色像素数}
    static std::pair<int, int> boundaryStats(const QImage& img)
    {
        int boundary = 0, dark = 0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                const QRgb px = img.pixel(x, y);
                if (qAlpha(px) < 8)
                    continue;
                bool isBoundary = false;
                const int dx[4] = { 1, -1, 0, 0 };
                const int dy[4] = { 0, 0, 1, -1 };
                for (int k = 0; k < 4; ++k) {
                    const int nx = x + dx[k], ny = y + dy[k];
                    if (nx < 0 || ny < 0 || nx >= img.width() || ny >= img.height()
                        || qAlpha(img.pixel(nx, ny)) < 8) {
                        isBoundary = true;
                        break;
                    }
                }
                if (!isBoundary)
                    continue;
                ++boundary;
                const int l = lumaOf(px);
                const int maxc = std::max({ qRed(px), qGreen(px), qBlue(px) });
                const int minc = std::min({ qRed(px), qGreen(px), qBlue(px) });
                const bool darkish = l < 95 || (l < 135 && (maxc - minc) < 60);
                if (darkish)
                    ++dark;
            }
        }
        return { boundary, dark };
    }

    // 内容像素的 16 级亮度分层（每层至少 2% 像素才算有效层级）
    static int lumaLevels(const QImage& img)
    {
        int histogram[16] = { 0 };
        int total = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                const QRgb px = img.pixel(x, y);
                if (qAlpha(px) < 8)
                    continue;
                ++histogram[lumaOf(px) / 16];
                ++total;
            }
        if (total == 0)
            return 0;
        int levels = 0;
        for (int b : histogram)
            if (b >= total / 50)
                ++levels;
        return levels;
    }

    static int contentColors(const QImage& img)
    {
        QSet<QRgb> colors;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                const QRgb px = img.pixel(x, y);
                if (qAlpha(px) > 8)
                    colors.insert(qRgb(qRed(px), qGreen(px), qBlue(px)));
            }
        return colors.size();
    }

    static QSet<QRgb> paletteOf(const QImage& img)
    {
        QSet<QRgb> colors;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                const QRgb px = img.pixel(x, y);
                if (qAlpha(px) > 8)
                    colors.insert(qRgb(qRed(px), qGreen(px), qBlue(px)));
            }
        return colors;
    }

    static int colorDist(QRgb a, QRgb b)
    {
        const int dr = int(qRed(a)) - int(qRed(b));
        const int dg = int(qGreen(a)) - int(qGreen(b));
        const int db = int(qBlue(a)) - int(qBlue(b));
        return dr * dr + dg * dg + db * db;
    }

    // 双向平均最近色距离（欧氏），0 = 色板完全一致
    static double paletteDrift(const QSet<QRgb>& a, const QSet<QRgb>& b)
    {
        if (a.isEmpty() || b.isEmpty())
            return 0.0;
        auto avgNearest = [](const QSet<QRgb>& from, const QSet<QRgb>& to) {
            double sum = 0.0;
            for (QRgb c : from) {
                int best = INT_MAX;
                for (QRgb t : to)
                    best = std::min(best, colorDist(c, t));
                sum += std::sqrt(double(best));
            }
            return sum / from.size();
        };
        return (avgNearest(a, b) + avgNearest(b, a)) / 2.0;
    }

    static int contentHeight(const QImage& img)
    {
        int y0 = img.height(), y1 = -1;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (qAlpha(img.pixel(x, y)) > 8) {
                    y0 = std::min(y0, y);
                    y1 = std::max(y1, y);
                    break;
                }
        return y1 >= y0 ? y1 - y0 + 1 : 0;
    }
};

} // namespace gc
