#pragma once

// ProjectStore：本地项目存储（技术路线 5.1 / 7.1）。
// 目录结构（引擎无关，技术路线 11）：
//   projects/<name>/
//     project.json                      项目与风格包
//     assets/<assetId>/
//       asset.json                      素材记录：规格、版本历史、母版、锁定帧
//       spec.json                       最近一次 AssetSpec
//       raw/                            模型原始输出（永不覆盖删除）
//       processed/                      标准化帧
//       exports/                        Sprite Sheet + 元数据
//       reviews/                        检查报告
// 版本管理原则：任何生成结果都产生新版本文件，旧版本永不覆盖（技术路线 7.1）。

#include "core/AssetSpec.h"
#include "core/ReviewReport.h"

#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace gc {

struct AssetVersion {
    QString versionId;
    QString filePath;   // 相对项目根目录
    QString kind;       // "raw" | "processed" | "master"
    QString direction;  // "down"/"up"/"left"/"right"/""（非角色）
    QString model;      // 适配器名
    qlonglong seed = 0;
    QDateTime created;
};

struct AssetRecord {
    QString id;
    QString name;
    AssetType type = AssetType::Item;
    QString masterVersionId;               // 角色身份母版（技术路线 8.1）
    QHash<QString, QString> lockedFrames;  // direction -> processed versionId
    QVector<AssetVersion> versions;
    AssetSpec lastSpec;
    QDateTime created;
    QVector<QRgb> palette;                 // 母版统一色板（角色多方向共用）

    QString masterFilePath() const
    {
        for (const AssetVersion& v : versions)
            if (v.versionId == masterVersionId)
                return v.filePath;
        return QString();
    }
};

class ProjectStore {
public:
    // 项目风格包（技术路线 5.1）：约束配色、光照、透视
    struct StylePack {
        int frameSize = 64;
        int paletteSize = 32;
        QString lightDirection = QStringLiteral("top-left");
        QString perspective = QStringLiteral("three-quarter top-down");
    };

    ProjectStore(const QString& projectsRoot, const QString& projectName);

    bool createOrOpen(QString* error = nullptr);

    QString projectName() const { return m_projectName; }
    QString projectDir() const { return m_projectDir; }
    QDir dir() const { return QDir(m_projectDir); }
    StylePack loadStylePack() const;

    QString assetDirPath(const QString& assetId) const;
    QString absolutePath(const QString& relative) const;

    // ---- 项目与素材记录 ----
    bool saveProjectMeta(int frameSize, int paletteSize);
    AssetRecord createAsset(const QString& name, AssetType type);
    bool saveAssetRecord(const AssetRecord& rec);
    AssetRecord loadAssetRecord(const QString& assetId) const;
    QVector<QString> listAssets() const;

    bool saveSpec(const QString& assetId, const AssetSpec& spec);
    AssetSpec loadSpec(const QString& assetId) const;

    // ---- 文件保存（永不覆盖：同名自动追加序号）----
    // 返回相对路径；versionId 由调用方生成
    QString saveRawImage(const QString& assetId, const QString& versionId,
                         const QByteArray& pngData, const QString& direction);
    QString saveProcessedImage(const QString& assetId, const QString& versionId,
                               const QImage& img, const QString& direction);
    QString saveExport(const QString& assetId, const QString& baseName,
                       const QImage& img);
    QString saveExportJson(const QString& assetId, const QString& baseName,
                           const QJsonObject& json);
    QString saveReviewReport(const QString& assetId, const ReviewReport& report);

private:
    static QString uniquePath(const QDir& d, const QString& baseName, const QString& ext);
    static AssetVersion versionFromJson(const QJsonObject& o);
    static QJsonObject versionToJson(const AssetVersion& v);

    QString m_root;
    QString m_projectName;
    QString m_projectDir;
};

} // namespace gc
