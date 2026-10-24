#pragma once

// AssetExporter：资产导出器（技术路线 11 —— 引擎无关优先，再转目标引擎格式）。
// 项目目录内永远保留一份存档导出（ProjectStore::saveExport），
// 本模块负责把最终通过版本导出到用户选择的目录与格式：
//   GenericSheet     通用 Sprite Sheet：PNG + JSON 元数据（引擎无关）
//   SeparateFrames   单帧 PNG 文件夹：每方向一张 + 索引 JSON
//   Godot4           Sprite Sheet + SpriteFrames .tres（动画/方向就绪，拖入即用）
//   TexturePackerJson Sprite Sheet + TexturePacker JSON Hash（Phaser / PIXI 等）

#include "core/AssetSpec.h"

#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace gc {

class AssetExporter {
public:
    enum class Format {
        GenericSheet,
        SeparateFrames,
        Godot4,
        TexturePackerJson
    };

    static QString formatName(Format f)
    {
        switch (f) {
        case Format::GenericSheet:      return QStringLiteral("通用 Sprite Sheet（PNG + JSON）");
        case Format::SeparateFrames:    return QStringLiteral("单帧 PNG 文件夹（每方向一张）");
        case Format::Godot4:            return QStringLiteral("Godot 4（Sheet + SpriteFrames .tres）");
        case Format::TexturePackerJson: return QStringLiteral("Phaser / PIXI（TexturePacker JSON）");
        }
        return QStringLiteral("通用");
    }

    static QString formatKey(Format f)
    {
        switch (f) {
        case Format::GenericSheet:      return QStringLiteral("generic");
        case Format::SeparateFrames:    return QStringLiteral("frames");
        case Format::Godot4:            return QStringLiteral("godot4");
        case Format::TexturePackerJson: return QStringLiteral("texturepacker");
        }
        return QStringLiteral("generic");
    }

    static Format formatFromKey(const QString& key)
    {
        if (key == QLatin1String("frames"))        return Format::SeparateFrames;
        if (key == QLatin1String("godot4"))        return Format::Godot4;
        if (key == QLatin1String("texturepacker")) return Format::TexturePackerJson;
        return Format::GenericSheet;
    }

    static QVector<Format> allFormats()
    {
        return { Format::GenericSheet, Format::SeparateFrames,
                 Format::Godot4, Format::TexturePackerJson };
    }

    struct SheetData {
        QImage sheet;                 // 已打包的图集（或单帧）
        int frameSize = 64;           // 单帧尺寸
        QVector<QString> rowDirNames; // 每行的方向名（down/up/... 或 "single"）
        QVector<int> rowFrameCounts;  // 每行帧数（MVP 每行 1 帧）
        QVector<QRgb> palette;
        QJsonObject meta;             // assetId / name / anchor / fps 等附加元数据
    };

    // 导出到 targetDir；返回写入的文件绝对路径列表。失败时返回空并写 error。
    static QStringList exportSheet(const SheetData& data, Format format,
                                   const QString& targetDir, const QString& baseName,
                                   QString* error);
};

} // namespace gc
