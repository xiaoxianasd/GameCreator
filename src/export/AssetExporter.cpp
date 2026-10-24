#include "export/AssetExporter.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

namespace gc {
namespace {

// 永不覆盖：同名自动追加 -1、-2 …
QString uniquePath(const QDir& dir, const QString& base, const QString& ext)
{
    QString path = dir.filePath(base + QLatin1Char('.') + ext);
    int i = 1;
    while (QFile::exists(path)) {
        path = dir.filePath(QStringLiteral("%1-%2.%3").arg(base).arg(i).arg(ext));
        ++i;
    }
    return path;
}

bool savePng(const QImage& img, const QString& path)
{
    return img.save(path, "PNG");
}

QString paletteHex(const QVector<QRgb>& palette)
{
    QStringList out;
    for (QRgb c : palette)
        out.append(QStringLiteral("#%1")
                       .arg(QString::number(c & 0xFFFFFF, 16)
                                .rightJustified(6, QLatin1Char('0'))
                                .toUpper()));
    return out.join(QStringLiteral(","));
}

QJsonObject genericMeta(const AssetExporter::SheetData& d)
{
    QJsonObject root = d.meta;
    root.insert(QStringLiteral("frameWidth"), d.frameSize);
    root.insert(QStringLiteral("frameHeight"), d.frameSize);
    {
        QJsonArray rows;
        for (int r = 0; r < d.rowDirNames.size(); ++r) {
            QJsonObject row;
            row.insert(QStringLiteral("direction"), d.rowDirNames[r]);
            row.insert(QStringLiteral("frames"), d.rowFrameCounts.isEmpty()
                                                    ? 1 : d.rowFrameCounts[r]);
            row.insert(QStringLiteral("row"), r);
            rows.append(row);
        }
        root.insert(QStringLiteral("rows"), rows);
    }
    root.insert(QStringLiteral("palette"), paletteHex(d.palette));
    root.insert(QStringLiteral("sheetWidth"), d.sheet.width());
    root.insert(QStringLiteral("sheetHeight"), d.sheet.height());
    return root;
}

// ---------- Godot 4 .tres（SpriteFrames，含每方向 AtlasTexture 动画）----------
QString godotTres(const AssetExporter::SheetData& d, const QString& sheetFileName)
{
    const int rows = d.rowDirNames.size();
    int subs = 0;
    for (int r = 0; r < rows; ++r)
        subs += d.rowFrameCounts.isEmpty() ? 1 : d.rowFrameCounts[r];

    QString out;
    QTextStream ts(&out);
    ts << QStringLiteral("[gd_resource type=\"SpriteFrames\" load_steps=%1 format=3]\n\n")
              .arg(subs + 2);
    ts << QStringLiteral("[ext_resource type=\"Texture2D\" path=\"res://%1\" id=\"1_tex\"]\n\n")
              .arg(sheetFileName);

    // 子资源：每个帧一个 AtlasTexture
    int idx = 0;
    for (int r = 0; r < rows; ++r) {
        const int count = d.rowFrameCounts.isEmpty() ? 1 : d.rowFrameCounts[r];
        for (int c = 0; c < count; ++c) {
            const int x = c * d.frameSize;
            const int y = r * d.frameSize;
            ts << QStringLiteral("[sub_resource type=\"AtlasTexture\" id=\"atlas_%1\"]\n")
                      .arg(idx);
            ts << QStringLiteral("atlas = ExtResource(\"1_tex\")\n");
            ts << QStringLiteral("region = Rect2(%1, %2, %3, %4)\n\n")
                      .arg(x).arg(y).arg(d.frameSize).arg(d.frameSize);
            ++idx;
        }
    }

    ts << QStringLiteral("[resource]\n");
    ts << QStringLiteral("animations = [");
    idx = 0;
    for (int r = 0; r < rows; ++r) {
        const QString dir = d.rowDirNames[r];
        const int count = d.rowFrameCounts.isEmpty() ? 1 : d.rowFrameCounts[r];
        ts << QStringLiteral("{\n");
        ts << QStringLiteral("\"frames\": [");
        for (int c = 0; c < count; ++c) {
            if (c > 0)
                ts << QStringLiteral(", ");
            ts << QStringLiteral("{\n\"duration\": 1.0,\n\"texture\": SubResource(\"atlas_%1\")\n}")
                      .arg(idx);
            ++idx;
        }
        ts << QStringLiteral("],\n");
        ts << QStringLiteral("\"loop\": true,\n");
        ts << QStringLiteral("\"name\": &\"idle_%1\",\n").arg(dir);
        ts << QStringLiteral("\"speed\": 8.0\n");
        ts << QStringLiteral("}");
        if (r < rows - 1)
            ts << QStringLiteral(", ");
    }
    ts << QStringLiteral("]\n");
    return out;
}

// ---------- TexturePacker JSON Hash（Phaser / PIXI）----------
QJsonObject texturePackerJson(const AssetExporter::SheetData& d, const QString& sheetFileName)
{
    QJsonObject frames;
    int y = 0;
    for (int r = 0; r < d.rowDirNames.size(); ++r) {
        const int count = d.rowFrameCounts.isEmpty() ? 1 : d.rowFrameCounts[r];
        const int x = 0;
        for (int c = 0; c < count; ++c) {
            const QString name = count > 1
                                     ? QStringLiteral("%1_%2.png").arg(d.rowDirNames[r]).arg(c)
                                     : QStringLiteral("%1.png").arg(d.rowDirNames[r]);
            QJsonObject f;
            {
                QJsonObject frame;
                frame.insert(QStringLiteral("x"), x + c * d.frameSize);
                frame.insert(QStringLiteral("y"), y);
                frame.insert(QStringLiteral("w"), d.frameSize);
                frame.insert(QStringLiteral("h"), d.frameSize);
                f.insert(QStringLiteral("frame"), frame);
            }
            f.insert(QStringLiteral("rotated"), false);
            f.insert(QStringLiteral("trimmed"), false);
            {
                QJsonObject sss;
                sss.insert(QStringLiteral("x"), 0);
                sss.insert(QStringLiteral("y"), 0);
                sss.insert(QStringLiteral("w"), d.frameSize);
                sss.insert(QStringLiteral("h"), d.frameSize);
                f.insert(QStringLiteral("spriteSourceSize"), sss);
            }
            {
                QJsonObject ss;
                ss.insert(QStringLiteral("w"), d.frameSize);
                ss.insert(QStringLiteral("h"), d.frameSize);
                f.insert(QStringLiteral("sourceSize"), ss);
            }
            {
                // 锚点：底部中心（脚底）
                QJsonObject pivot;
                pivot.insert(QStringLiteral("x"), 0.5);
                pivot.insert(QStringLiteral("y"), 1.0);
                f.insert(QStringLiteral("pivot"), pivot);
            }
            frames.insert(name, f);
        }
        y += d.frameSize;
    }

    QJsonObject meta;
    meta.insert(QStringLiteral("app"), QStringLiteral("GameCreator"));
    meta.insert(QStringLiteral("image"), sheetFileName);
    meta.insert(QStringLiteral("scale"), QStringLiteral("1"));
    {
        QJsonObject size;
        size.insert(QStringLiteral("w"), d.sheet.width());
        size.insert(QStringLiteral("h"), d.sheet.height());
        meta.insert(QStringLiteral("size"), size);
    }

    QJsonObject root;
    root.insert(QStringLiteral("frames"), frames);
    root.insert(QStringLiteral("meta"), meta);
    return root;
}

} // namespace

QStringList AssetExporter::exportSheet(const SheetData& data, Format format,
                                       const QString& targetDir, const QString& baseName,
                                       QString* error)
{
    QStringList written;
    if (data.sheet.isNull()) {
        if (error) *error = QStringLiteral("图集为空，无法导出");
        return written;
    }
    QDir dir(targetDir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (error) *error = QStringLiteral("无法创建导出目录：%1").arg(targetDir);
        return written;
    }

    switch (format) {
    case Format::GenericSheet: {
        const QString sheetPath = uniquePath(dir, baseName + QStringLiteral("-sheet"),
                                              QStringLiteral("png"));
        if (!savePng(data.sheet, sheetPath)) {
            if (error) *error = QStringLiteral("PNG 写入失败：%1").arg(sheetPath);
            return written;
        }
        written.append(sheetPath);
        const QString jsonPath = uniquePath(dir, baseName + QStringLiteral("-sheet"),
                                             QStringLiteral("json"));
        QFile jf(jsonPath);
        if (jf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            jf.write(QJsonDocument(genericMeta(data)).toJson(QJsonDocument::Indented));
            written.append(jsonPath);
        }
        break;
    }

    case Format::SeparateFrames: {
        // 单帧文件夹：从图集重新裁出每方向单帧
        QDir sub(dir.filePath(baseName + QStringLiteral("_frames")));
        if (!sub.exists() && !sub.mkpath(QStringLiteral("."))) {
            if (error) *error = QStringLiteral("无法创建帧目录");
            return written;
        }
        QJsonObject index = data.meta;
        {
            QJsonArray files;
            for (int r = 0; r < data.rowDirNames.size(); ++r) {
                const int count = data.rowFrameCounts.isEmpty()
                                      ? 1 : data.rowFrameCounts[r];
                for (int c = 0; c < count; ++c) {
                    const QString stem = count > 1
                        ? QStringLiteral("%1_%2_%3").arg(baseName, data.rowDirNames[r]).arg(c)
                        : QStringLiteral("%1_%2").arg(baseName, data.rowDirNames[r]);
                    const QImage frame = data.sheet.copy(c * data.frameSize, r * data.frameSize,
                                                         data.frameSize, data.frameSize);
                    const QString path = uniquePath(sub, stem, QStringLiteral("png"));
                    if (savePng(frame, path)) {
                        written.append(path);
                        files.append(QDir(targetDir).relativeFilePath(path));
                    }
                }
            }
            index.insert(QStringLiteral("files"), files);
        }
        index.insert(QStringLiteral("palette"), paletteHex(data.palette));
        const QString jsonPath = uniquePath(dir, baseName, QStringLiteral("json"));
        QFile jf(jsonPath);
        if (jf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            jf.write(QJsonDocument(index).toJson(QJsonDocument::Indented));
            written.append(jsonPath);
        }
        break;
    }

    case Format::Godot4: {
        const QString sheetName = baseName + QStringLiteral("-sheet.png");
        const QString sheetPath = uniquePath(dir, baseName + QStringLiteral("-sheet"),
                                              QStringLiteral("png"));
        if (!savePng(data.sheet, sheetPath)) {
            if (error) *error = QStringLiteral("PNG 写入失败：%1").arg(sheetPath);
            return written;
        }
        written.append(sheetPath);
        // SpriteFrames 资源：动画按 idle_<方向> 组织
        const QString tresName = uniquePath(dir, baseName, QStringLiteral("tres"));
        QFile tf(tresName);
        if (tf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            tf.write(godotTres(data, sheetName).toUtf8());
            written.append(tresName);
        }
        // 使用说明（导入设置：像素风必须关滤镜）
        const QString readmePath = uniquePath(dir, baseName + QStringLiteral("-godot说明"),
                                               QStringLiteral("txt"));
        QFile rf(readmePath);
        if (rf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QTextStream rs(&rf); // Qt6 默认 UTF-8
            rs << QStringLiteral("Godot 4 使用说明\n");
            rs << QStringLiteral("1. 把 %1 和 .tres 文件一起复制到你的 Godot 项目目录（保持同目录）。\n").arg(sheetName);
            rs << QStringLiteral("2. 选中 %1，在 Import 面板把 Filter 关掉（Nearest），点击 Reimport。\n").arg(sheetName);
            rs << QStringLiteral("   建议同时 preset 为 2D Pixel，避免缩放模糊。\n");
            rs << QStringLiteral("3. AnimatedSprite2D 的 SpriteFrames 属性选择 %1.tres，即可播放 idle_down / idle_up 等动画。\n").arg(baseName);
            rs << QStringLiteral("4. 锚点为底部中心：节点的 Offset 请按脚底对齐（pivot y=1）。\n");
            written.append(readmePath);
        }
        break;
    }

    case Format::TexturePackerJson: {
        const QString sheetName = baseName + QStringLiteral("-sheet.png");
        const QString sheetPath = uniquePath(dir, baseName + QStringLiteral("-sheet"),
                                              QStringLiteral("png"));
        if (!savePng(data.sheet, sheetPath)) {
            if (error) *error = QStringLiteral("PNG 写入失败：%1").arg(sheetPath);
            return written;
        }
        written.append(sheetPath);
        const QString jsonPath = uniquePath(dir, baseName, QStringLiteral("json"));
        QFile jf(jsonPath);
        if (jf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            jf.write(QJsonDocument(texturePackerJson(data, sheetName)).toJson(QJsonDocument::Indented));
            written.append(jsonPath);
        }
        break;
    }
    }
    return written;
}

} // namespace gc
