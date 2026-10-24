#pragma once

// DeterministicChecks：确定性程序检查（技术路线 7.2）。
// 只做无需模型的硬性检查；可通过程序修复的问题已在素材工厂中修复，
// 这里输出的是「标准化之后仍不满足 AssetSpec」的问题。

#include "core/AssetSpec.h"
#include "core/ReviewReport.h"

#include <QImage>
#include <QSet>
#include <QString>
#include <QVector>

#include <algorithm>

namespace gc {

class DeterministicChecks {
public:
    struct FrameEntry {
        Direction dir = Direction::Down;
        QImage image;   // 标准化后的帧
    };

    // spec.directions 中每个方向都必须有对应帧（角色）；
    // 物品/场景只检查单帧。
    static ReviewReport run(const AssetSpec& spec, const QVector<FrameEntry>& frames)
    {
        ReviewReport report;
        report.versionId = spec.id;
        report.stage = QStringLiteral("deterministic");
        report.created = QDateTime::currentDateTime();

        double weightTotal = 0.0;
        double weightPass = 0.0;

        // ---- 1. 方向齐全性 ----
        if (spec.type == AssetType::Character) {
            for (Direction d : spec.directions) {
                const bool has = std::any_of(frames.begin(), frames.end(),
                                             [d](const FrameEntry& f) { return f.dir == d && !f.image.isNull(); });
                weightTotal += 2.0;
                if (has) {
                    weightPass += 2.0;
                } else {
                    addIssue(report, QStringLiteral("missing-frame"), Severity::Error,
                             QStringLiteral("缺少方向帧：%1").arg(directionToDisplay(d)));
                }
            }
        } else if (frames.isEmpty() || frames.first().image.isNull()) {
            addIssue(report, QStringLiteral("missing-frame"), Severity::Error,
                     QStringLiteral("缺少素材帧"));
            weightTotal += 2.0;
        } else {
            weightTotal += 2.0;
            weightPass += 2.0;
        }

        for (const FrameEntry& f : frames) {
            const QString dirName = spec.type == AssetType::Character
                                        ? directionToDisplay(f.dir) : QStringLiteral("单帧");
            const QImage& img = f.image;
            if (img.isNull()) {
                addIssue(report, QStringLiteral("null-frame"), Severity::Error,
                         QStringLiteral("%1：帧图像为空").arg(dirName));
                weightTotal += 2.0;
                continue;
            }

            // ---- 2. 帧尺寸 ----
            weightTotal += 2.0;
            if (img.width() == spec.frameSize && img.height() == spec.frameSize) {
                weightPass += 2.0;
            } else {
                addIssue(report, QStringLiteral("size-mismatch"), Severity::Error,
                         QStringLiteral("%1：帧尺寸 %2x%3，要求 %4x%4")
                             .arg(dirName).arg(img.width()).arg(img.height()).arg(spec.frameSize));
            }

            // ---- 3. 内容存在与包围盒 ----
            weightTotal += 2.0;
            const QRect bbox = contentBBox(img);
            if (bbox.isNull()) {
                addIssue(report, QStringLiteral("empty-content"), Severity::Error,
                         QStringLiteral("%1：帧内容为空（全透明）").arg(dirName));
                continue;
            }
            weightPass += 2.0;

            // ---- 4. 内容不贴边（透明边缘干净）----
            weightTotal += 1.0;
            const bool touchTop = bbox.top() == 0;
            const bool touchLeft = bbox.left() == 0;
            const bool touchRight = bbox.right() == img.width() - 1;
            const bool touchBottom = bbox.bottom() >= img.height() - 1;
            if (touchTop || touchLeft || touchRight || touchBottom) {
                addIssue(report, QStringLiteral("content-touch-edge"), Severity::Warning,
                         QStringLiteral("%1：内容贴近画布边缘，可能在动画中产生裁切风险").arg(dirName));
            } else {
                weightPass += 1.0;
            }

            // ---- 5. 调色板颜色数 ----
            weightTotal += 1.0;
            const int colors = countColors(img);
            const int limit = spec.type == AssetType::Scene
                                  ? qMax(spec.paletteSize * 2, 48)
                                  : spec.paletteSize;
            if (colors <= limit) {
                weightPass += 1.0;
            } else {
                addIssue(report, QStringLiteral("palette-overflow"), Severity::Warning,
                         QStringLiteral("%1：颜色数 %2 超过上限 %3，建议提高量化强度")
                             .arg(dirName).arg(colors).arg(limit));
            }

            // ---- 6. 脚底锚点对齐（角色）----
            if (spec.type == AssetType::Character) {
                weightTotal += 2.0;
                // 期望：内容底边位于倒数第 2~4 行（留 1px 呼吸边）
                const int bottomMargin = img.height() - 1 - bbox.bottom();
                if (bottomMargin >= 1 && bottomMargin <= 4) {
                    weightPass += 2.0;
                } else {
                    addIssue(report, QStringLiteral("anchor-misaligned"), Severity::Warning,
                             QStringLiteral("%1：脚底锚点偏移（底部留白 %2px，期望 1~4px），多方向动画可能跳动")
                                 .arg(dirName).arg(bottomMargin));
                }
            }
        }

        report.score = weightTotal > 0 ? weightPass / weightTotal : 0.0;
        report.passed = !hasError(report);
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

    static QRect contentBBox(const QImage& img)
    {
        int x0 = img.width(), y0 = img.height(), x1 = -1, y1 = -1;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                if (qAlpha(img.pixel(x, y)) > 8) {
                    if (x < x0) x0 = x;
                    if (y < y0) y0 = y;
                    if (x > x1) x1 = x;
                    if (y > y1) y1 = y;
                }
            }
        if (x1 < x0)
            return QRect();
        return QRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    }

    static int countColors(const QImage& img)
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
};

} // namespace gc
