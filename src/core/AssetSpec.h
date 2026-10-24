#pragma once

// AssetSpec：单个素材的结构化需求（技术路线 5.2）
// 它是「生成 → 标准化 → 检查 → 导出」全流程的统一依据。

#include "Common.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace gc {

struct AssetSpec {
    QString id;                 // spec 稳定 ID
    QString assetId;            // 所属素材（可为空，生成时由编排器补齐）
    AssetType type = AssetType::Item;

    // 提示词与排除项
    QString prompt;
    QString negativePrompt;
    QStringList referenceImages; // 参考图绝对路径（母版/设计稿）

    // 方向与动作（MVP：四方向 + 待机单帧）
    QVector<Direction> directions = { Direction::Down };

    // 输出规格
    int frameSize   = 64;   // 目标单帧像素尺寸（正方形）
    int modelWidth  = 512;  // 模型输出分辨率（高分辨率母版，之后降采样）
    int modelHeight = 512;
    int candidateCount = 4;  // 每个方向生成的候选数量

    // 标准化参数
    bool sourceHasAlpha = false; // 模型输出是否已带透明通道
    int  paletteSize    = 32;    // 颜色量化后的最大颜色数
    bool useDither      = false; // 量化时是否使用抖动（默认关闭，像素风更干净）
    QString anchor      = QStringLiteral("bottom-center"); // 角色脚底锚点

    // 风格包约束（来自项目 StylePack，参与提示词与工作流）
    QString styleLight       = QStringLiteral("top-left");          // 光照方向
    QString stylePerspective = QStringLiteral("three-quarter top-down"); // 透视方式

    // 审查与重试预算（技术路线 7.6）
    int    maxRounds    = 3;
    double minScore     = 0.6;

    QJsonObject toJson() const
    {
        QJsonObject o;
        o.insert(QStringLiteral("id"), id);
        o.insert(QStringLiteral("assetId"), assetId);
        o.insert(QStringLiteral("type"), assetTypeToString(type));
        o.insert(QStringLiteral("prompt"), prompt);
        o.insert(QStringLiteral("negativePrompt"), negativePrompt);
        {
            QJsonArray a;
            for (const QString& r : referenceImages) a.append(r);
            o.insert(QStringLiteral("referenceImages"), a);
        }
        {
            QJsonArray a;
            for (Direction d : directions) a.append(directionToString(d));
            o.insert(QStringLiteral("directions"), a);
        }
        o.insert(QStringLiteral("frameSize"), frameSize);
        o.insert(QStringLiteral("modelWidth"), modelWidth);
        o.insert(QStringLiteral("modelHeight"), modelHeight);
        o.insert(QStringLiteral("candidateCount"), candidateCount);
        o.insert(QStringLiteral("sourceHasAlpha"), sourceHasAlpha);
        o.insert(QStringLiteral("paletteSize"), paletteSize);
        o.insert(QStringLiteral("useDither"), useDither);
        o.insert(QStringLiteral("anchor"), anchor);
        o.insert(QStringLiteral("styleLight"), styleLight);
        o.insert(QStringLiteral("stylePerspective"), stylePerspective);
        o.insert(QStringLiteral("maxRounds"), maxRounds);
        o.insert(QStringLiteral("minScore"), minScore);
        return o;
    }

    static AssetSpec fromJson(const QJsonObject& o)
    {
        AssetSpec s;
        s.id       = o.value(QStringLiteral("id")).toString();
        s.assetId  = o.value(QStringLiteral("assetId")).toString();
        s.type     = assetTypeFromString(o.value(QStringLiteral("type")).toString());
        s.prompt   = o.value(QStringLiteral("prompt")).toString();
        s.negativePrompt = o.value(QStringLiteral("negativePrompt")).toString();
        s.referenceImages.clear();
        for (const QJsonValue& v : o.value(QStringLiteral("referenceImages")).toArray())
            s.referenceImages.append(v.toString());
        s.directions.clear();
        for (const QJsonValue& v : o.value(QStringLiteral("directions")).toArray())
            s.directions.append(directionFromString(v.toString()));
        s.frameSize      = o.value(QStringLiteral("frameSize")).toInt(64);
        s.modelWidth     = o.value(QStringLiteral("modelWidth")).toInt(512);
        s.modelHeight    = o.value(QStringLiteral("modelHeight")).toInt(512);
        s.candidateCount = o.value(QStringLiteral("candidateCount")).toInt(4);
        s.sourceHasAlpha = o.value(QStringLiteral("sourceHasAlpha")).toBool(false);
        s.paletteSize    = o.value(QStringLiteral("paletteSize")).toInt(32);
        s.useDither      = o.value(QStringLiteral("useDither")).toBool(false);
        s.anchor         = o.value(QStringLiteral("anchor")).toString(QStringLiteral("bottom-center"));
        s.styleLight     = o.value(QStringLiteral("styleLight")).toString(QStringLiteral("top-left"));
        s.stylePerspective = o.value(QStringLiteral("stylePerspective")).toString(QStringLiteral("three-quarter top-down"));
        s.maxRounds      = o.value(QStringLiteral("maxRounds")).toInt(3);
        s.minScore       = o.value(QStringLiteral("minScore")).toDouble(0.6);
        return s;
    }
};

} // namespace gc
