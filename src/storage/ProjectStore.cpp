#include "storage/ProjectStore.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace gc {

ProjectStore::ProjectStore(const QString& projectsRoot, const QString& projectName)
    : m_root(projectsRoot)
    , m_projectName(projectName)
{
    m_projectDir = QDir(m_root).filePath(m_projectName);
}

QString ProjectStore::resolveProjectsRoot()
{
    const QByteArray env = qgetenv("GAMECREATOR_PROJECTS_DIR");
    if (!env.isEmpty())
        return QDir::cleanPath(QString::fromLocal8Bit(env));

    const QDir appDir(QCoreApplication::applicationDirPath());
    const QDir repoRoot(appDir.absoluteFilePath(QStringLiteral("..")));
    if (QFileInfo::exists(repoRoot.filePath(QStringLiteral("CMakeLists.txt")))
        && QFileInfo::exists(repoRoot.filePath(QStringLiteral("src"))))
        return QDir::cleanPath(repoRoot.filePath(QStringLiteral("projects")));

    const QString portable = appDir.filePath(QStringLiteral("projects"));
    if (QFileInfo(appDir.path()).isWritable())
        return QDir::cleanPath(portable);

    return QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                           + QStringLiteral("/projects"));
}

bool ProjectStore::migrateLegacyProjects(const QString& legacyRoot, const QString& targetRoot,
                                         QString* message)
{
    const QString legacy = QDir::cleanPath(legacyRoot);
    const QString target = QDir::cleanPath(targetRoot);
    if (legacy == target || !QDir(legacy).exists())
        return false;

    // 目标已有内容（项目/文件）时绝不覆盖
    QDir targetDir(target);
    if (targetDir.exists()) {
        const QStringList existing =
            targetDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Files);
        if (!existing.isEmpty())
            return false;
    }

    QDir legacyDir(legacy);
    if (legacyDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Files).isEmpty())
        return false;

    if (!QDir().mkpath(target)) {
        if (message)
            *message = QStringLiteral("旧项目迁移失败：无法创建 %1")
                           .arg(QDir::toNativeSeparators(target));
        return false;
    }

    QDirIterator it(legacy, QDirIterator::Subdirectories);
    int copied = 0;
    while (it.hasNext()) {
        const QString src = it.next();
        const QString rel = legacyDir.relativeFilePath(src);
        const QFileInfo fi(src);
        if (fi.isDir()) {
            QDir(target).mkpath(rel);
            continue;
        }
        const QString dst = QDir(target).filePath(rel);
        QDir().mkpath(QFileInfo(dst).path());
        if (QFileInfo::exists(dst))
            continue;
        if (!QFile::copy(src, dst)) {
            // 目标是空目录，失败时清掉半成品，避免下次启动误判“已迁移”
            QDir(target).removeRecursively();
            if (message)
                *message = QStringLiteral("旧项目迁移中断：复制失败 %1")
                               .arg(QDir::toNativeSeparators(src));
            return false;
        }
        ++copied;
    }

    if (message) {
        *message = QStringLiteral("已将旧项目从 %1 迁移到 %2（%3 个文件，旧目录保留）")
                       .arg(QDir::toNativeSeparators(legacy),
                            QDir::toNativeSeparators(target))
                       .arg(copied);
    }
    return true;
}

bool ProjectStore::createOrOpen(QString* error)
{
    QDir root(m_root);
    if (!root.exists() && !root.mkpath(QStringLiteral("."))) {
        if (error) *error = QStringLiteral("无法创建项目根目录：%1").arg(m_root);
        return false;
    }
    const QStringList subDirs = { QStringLiteral("assets") };
    for (const QString& s : subDirs) {
        if (!root.mkpath(m_projectName + QLatin1Char('/') + s)) {
            if (error) *error = QStringLiteral("无法创建项目目录：%1/%2").arg(m_projectDir, s);
            return false;
        }
    }
    if (!QFileInfo::exists(m_projectDir + QStringLiteral("/project.json")))
        return saveProjectMeta(64, 32);
    return true;
}

bool ProjectStore::saveProjectMeta(int frameSize, int paletteSize)
{
    QJsonObject style;
    style.insert(QStringLiteral("frameSize"), frameSize);
    style.insert(QStringLiteral("paletteSize"), paletteSize);
    style.insert(QStringLiteral("lightDirection"), QStringLiteral("top-left"));
    style.insert(QStringLiteral("perspective"), QStringLiteral("three-quarter top-down"));
    QJsonObject root;
    root.insert(QStringLiteral("name"), m_projectName);
    root.insert(QStringLiteral("createdAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    root.insert(QStringLiteral("stylePack"), style);

    QFile f(m_projectDir + QStringLiteral("/project.json"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

ProjectStore::StylePack ProjectStore::loadStylePack() const
{
    StylePack sp;
    QFile f(m_projectDir + QStringLiteral("/project.json"));
    if (!f.open(QIODevice::ReadOnly))
        return sp;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject style = root.value(QStringLiteral("stylePack")).toObject();
    sp.frameSize      = style.value(QStringLiteral("frameSize")).toInt(sp.frameSize);
    sp.paletteSize    = style.value(QStringLiteral("paletteSize")).toInt(sp.paletteSize);
    sp.lightDirection = style.value(QStringLiteral("lightDirection")).toString(sp.lightDirection);
    sp.perspective    = style.value(QStringLiteral("perspective")).toString(sp.perspective);
    return sp;
}

QString ProjectStore::assetDirPath(const QString& assetId) const
{
    return m_projectDir + QStringLiteral("/assets/") + assetId;
}

QString ProjectStore::absolutePath(const QString& relative) const
{
    return QDir::cleanPath(m_projectDir + QLatin1Char('/') + relative);
}

AssetRecord ProjectStore::createAsset(const QString& name, AssetType type)
{
    AssetRecord rec;
    rec.id = newId(QStringLiteral("ast"));
    rec.name = name.isEmpty() ? assetTypeToDisplay(type) + QDateTime::currentDateTime().toString(QStringLiteral("-MMddHHmm")) : name;
    rec.type = type;
    rec.created = QDateTime::currentDateTime();
    saveAssetRecord(rec);
    return rec;
}

QString ProjectStore::uniquePath(const QDir& d, const QString& baseName, const QString& ext)
{
    QString path = d.filePath(baseName + QLatin1Char('.') + ext);
    int i = 1;
    while (QFileInfo::exists(path)) {
        path = d.filePath(QStringLiteral("%1-%2.%3").arg(baseName).arg(i).arg(ext));
        ++i;
    }
    return path;
}

AssetVersion ProjectStore::versionFromJson(const QJsonObject& o)
{
    AssetVersion v;
    v.versionId = o.value(QStringLiteral("versionId")).toString();
    v.filePath  = o.value(QStringLiteral("filePath")).toString();
    v.kind      = o.value(QStringLiteral("kind")).toString();
    v.direction = o.value(QStringLiteral("direction")).toString();
    v.model     = o.value(QStringLiteral("model")).toString();
    v.seed      = o.value(QStringLiteral("seed")).toVariant().toLongLong();
    v.created   = QDateTime::fromString(o.value(QStringLiteral("created")).toString(), Qt::ISODateWithMs);
    return v;
}

QJsonObject ProjectStore::versionToJson(const AssetVersion& v)
{
    QJsonObject o;
    o.insert(QStringLiteral("versionId"), v.versionId);
    o.insert(QStringLiteral("filePath"), v.filePath);
    o.insert(QStringLiteral("kind"), v.kind);
    o.insert(QStringLiteral("direction"), v.direction);
    o.insert(QStringLiteral("model"), v.model);
    o.insert(QStringLiteral("seed"), double(v.seed));
    o.insert(QStringLiteral("created"), v.created.toString(Qt::ISODateWithMs));
    return o;
}

bool ProjectStore::saveAssetRecord(const AssetRecord& rec)
{
    QDir d(assetDirPath(rec.id));
    d.mkpath(QStringLiteral("."));

    QJsonObject root;
    root.insert(QStringLiteral("id"), rec.id);
    root.insert(QStringLiteral("name"), rec.name);
    root.insert(QStringLiteral("type"), assetTypeToString(rec.type));
    root.insert(QStringLiteral("masterVersionId"), rec.masterVersionId);
    root.insert(QStringLiteral("created"), rec.created.toString(Qt::ISODateWithMs));
    {
        QJsonObject locked;
        for (auto it = rec.lockedFrames.begin(); it != rec.lockedFrames.end(); ++it)
            locked.insert(it.key(), it.value());
        root.insert(QStringLiteral("lockedFrames"), locked);
    }
    QJsonArray versions;
    for (const AssetVersion& v : rec.versions)
        versions.append(versionToJson(v));
    root.insert(QStringLiteral("versions"), versions);
    root.insert(QStringLiteral("lastSpec"), rec.lastSpec.toJson());
    {
        QJsonArray pal;
        for (QRgb c : rec.palette)
            pal.append(QString::number(c, 16));
        root.insert(QStringLiteral("palette"), pal);
    }

    QFile f(d.filePath(QStringLiteral("asset.json")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

AssetRecord ProjectStore::loadAssetRecord(const QString& assetId) const
{
    AssetRecord rec;
    rec.id = assetId;
    QFile f(assetDirPath(assetId) + QStringLiteral("/asset.json"));
    if (!f.open(QIODevice::ReadOnly))
        return rec;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    rec.id = root.value(QStringLiteral("id")).toString(assetId);
    rec.name = root.value(QStringLiteral("name")).toString();
    rec.type = assetTypeFromString(root.value(QStringLiteral("type")).toString());
    rec.masterVersionId = root.value(QStringLiteral("masterVersionId")).toString();
    rec.created = QDateTime::fromString(root.value(QStringLiteral("created")).toString(), Qt::ISODateWithMs);
    const QJsonObject locked = root.value(QStringLiteral("lockedFrames")).toObject();
    for (auto it = locked.begin(); it != locked.end(); ++it)
        rec.lockedFrames.insert(it.key(), it.value().toString());
    for (const QJsonValue& v : root.value(QStringLiteral("versions")).toArray())
        rec.versions.append(versionFromJson(v.toObject()));
    rec.lastSpec = AssetSpec::fromJson(root.value(QStringLiteral("lastSpec")).toObject());
    rec.palette.clear();
    for (const QJsonValue& v : root.value(QStringLiteral("palette")).toArray())
        rec.palette.append(QRgb(v.toString().toUInt(nullptr, 16)));
    return rec;
}

QVector<QString> ProjectStore::listAssets() const
{
    QVector<QString> out;
    QDir d(m_projectDir + QStringLiteral("/assets"));
    const QFileInfoList entries =
        d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (const QFileInfo& fi : entries)
        out.append(fi.fileName());
    return out;
}

bool ProjectStore::saveSpec(const QString& assetId, const AssetSpec& spec)
{
    QDir d(assetDirPath(assetId));
    d.mkpath(QStringLiteral("."));
    QFile f(d.filePath(QStringLiteral("spec.json")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(spec.toJson()).toJson(QJsonDocument::Indented));
    return true;
}

AssetSpec ProjectStore::loadSpec(const QString& assetId) const
{
    QFile f(assetDirPath(assetId) + QStringLiteral("/spec.json"));
    if (!f.open(QIODevice::ReadOnly))
        return AssetSpec();
    return AssetSpec::fromJson(QJsonDocument::fromJson(f.readAll()).object());
}

QString ProjectStore::saveRawImage(const QString& assetId, const QString& versionId,
                                   const QByteArray& pngData, const QString& direction)
{
    QDir d(assetDirPath(assetId));
    d.mkpath(QStringLiteral("raw"));
    QDir sub(d.filePath(QStringLiteral("raw")));
    const QString base = direction.isEmpty()
                             ? versionId
                             : versionId + QStringLiteral("-") + direction;
    const QString path = uniquePath(sub, base, QStringLiteral("png"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return QString();
    f.write(pngData);
    return QDir(m_projectDir).relativeFilePath(path);
}

QString ProjectStore::saveProcessedImage(const QString& assetId, const QString& versionId,
                                         const QImage& img, const QString& direction)
{
    QDir d(assetDirPath(assetId));
    d.mkpath(QStringLiteral("processed"));
    QDir sub(d.filePath(QStringLiteral("processed")));
    const QString base = direction.isEmpty()
                             ? versionId
                             : versionId + QStringLiteral("-") + direction;
    const QString path = uniquePath(sub, base, QStringLiteral("png"));
    if (!img.save(path, "PNG"))
        return QString();
    return QDir(m_projectDir).relativeFilePath(path);
}

QString ProjectStore::saveExport(const QString& assetId, const QString& baseName,
                                 const QImage& img)
{
    QDir d(assetDirPath(assetId));
    d.mkpath(QStringLiteral("exports"));
    QDir sub(d.filePath(QStringLiteral("exports")));
    const QString path = uniquePath(sub, baseName, QStringLiteral("png"));
    if (!img.save(path, "PNG"))
        return QString();
    return QDir(m_projectDir).relativeFilePath(path);
}

QString ProjectStore::saveExportJson(const QString& assetId, const QString& baseName,
                                     const QJsonObject& json)
{
    QDir d(assetDirPath(assetId));
    d.mkpath(QStringLiteral("exports"));
    QDir sub(d.filePath(QStringLiteral("exports")));
    const QString path = uniquePath(sub, baseName, QStringLiteral("json"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    f.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
    return QDir(m_projectDir).relativeFilePath(path);
}

QString ProjectStore::saveReviewReport(const QString& assetId, const ReviewReport& report)
{
    QDir d(assetDirPath(assetId));
    d.mkpath(QStringLiteral("reviews"));
    QDir sub(d.filePath(QStringLiteral("reviews")));
    const QString path = uniquePath(sub, timeStampTag(), QStringLiteral("json"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    f.write(QJsonDocument(report.toJson()).toJson(QJsonDocument::Indented));
    return QDir(m_projectDir).relativeFilePath(path);
}

} // namespace gc
