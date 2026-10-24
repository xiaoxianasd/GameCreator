// PipelineTest：无 GUI 的端到端流水线自测（mock 模式）。
// 验证：任务编排 → 候选生成 → 母版设定 → 四方向生成（引用母版）
//       → 标准化（背景去除/像素化/量化/锚点）→ 锁定 → 检查 → Sprite Sheet 导出。

#include "checks/DeterministicChecks.h"
#include "checks/VisualHeuristics.h"
#include "export/AssetExporter.h"
#include "factory/AssetFactory.h"
#include "models/ModelRouter.h"
#include "orchestrator/TaskOrchestrator.h"
#include "storage/ProjectStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QTimer>
#include <iostream>

using namespace gc;

namespace {

ProjectStore* g_store = nullptr;
QString g_assetId;
int g_phase = 0; // 0=母版轮 1=方向轮
QVector<QPair<QString, QString>> g_rawByDir; // direction -> raw abs path (最新)

void exportAndCheck()
{
    AssetRecord rec = g_store->loadAssetRecord(g_assetId);
    AssetSpec spec = rec.lastSpec;

    const Direction order[4] = { Direction::Down, Direction::Up, Direction::Left, Direction::Right };
    QVector<QVector<QImage>> rows;
    QVector<QVector<Direction>> rowDirs;
    QVector<DeterministicChecks::FrameEntry> checkFrames;
    QVector<QRgb> palette;

    for (Direction d : order) {
        const QString key = directionToString(d);
        const QString vid = rec.lockedFrames.value(key);
        if (vid.isEmpty())
            continue;
        QString path;
        for (const AssetVersion& v : rec.versions)
            if (v.versionId == vid)
                path = g_store->absolutePath(v.filePath);
        QImage frame(path);
        if (frame.isNull()) {
            std::cout << "[FAIL] 读取锁定帧失败: " << path.toStdString() << std::endl;
            QCoreApplication::exit(2);
            return;
        }
        rows.append({ frame });
        rowDirs.append({ d });
        DeterministicChecks::FrameEntry e;
        e.dir = d;
        e.image = frame;
        checkFrames.append(e);
        if (palette.isEmpty())
            palette = AssetFactory::buildPalette(frame, spec.paletteSize);
    }

    const ReviewReport det = DeterministicChecks::run(spec, checkFrames);
    QVector<VisualHeuristics::FrameEntry> visFrames;
    for (const DeterministicChecks::FrameEntry& f : checkFrames) {
        VisualHeuristics::FrameEntry vf;
        vf.dir = f.dir;
        vf.image = f.image;
        visFrames.append(vf);
    }
    const ReviewReport vis = VisualHeuristics::run(spec, visFrames);
    ReviewReport report = det;
    report.issues += vis.issues;
    report.score = (det.score + vis.score) / 2.0;
    report.passed = det.passed && vis.passed;
    report.stage = QStringLiteral("deterministic+vision-heuristic");
    g_store->saveReviewReport(g_assetId, report);
    std::cout << "确定性检查得分: " << det.score << " | 视觉启发式得分: " << vis.score
              << " | 综合: " << report.score << " 通过: " << (report.passed ? "yes" : "no") << std::endl;
    for (const ReviewIssue& i : report.issues)
        std::cout << "  [" << i.severity.toStdString() << "] " << i.id.toStdString()
                  << " - " << i.message.toStdString() << std::endl;

    const QImage sheet = AssetFactory::packSheet(rows, spec.frameSize);
    const QString sheetPath = g_store->saveExport(g_assetId, QStringLiteral("test-sheet"), sheet);
    g_store->saveExportJson(g_assetId, QStringLiteral("test-sheet"),
                            AssetFactory::sheetMetadata(spec, rowDirs, spec.frameSize, palette));
    std::cout << "SpriteSheet: " << QDir::toNativeSeparators(
                     g_store->absolutePath(sheetPath)).toStdString()
              << " (" << sheet.width() << "x" << sheet.height() << ", rows=" << rows.size() << ")"
              << std::endl;

    // ---- 导出器验证：四种格式写入用户目录 ----
    AssetExporter::SheetData ed;
    ed.sheet = sheet;
    ed.frameSize = spec.frameSize;
    for (const QVector<Direction>& rd : rowDirs)
        ed.rowDirNames.append(directionToString(rd.first()));
    for (int i = 0; i < rowDirs.size(); ++i)
        ed.rowFrameCounts.append(1);
    ed.palette = palette;
    ed.meta.insert(QStringLiteral("assetName"), QStringLiteral("test"));
    int exportOk = 0;
    const QString exportRoot = QCoreApplication::applicationDirPath()
                             + QStringLiteral("/test-exports");
    for (AssetExporter::Format fmt : AssetExporter::allFormats()) {
        const QString dir = exportRoot + QLatin1Char('/') + AssetExporter::formatKey(fmt);
        QString err;
        const QStringList files = AssetExporter::exportSheet(ed, fmt, dir, QStringLiteral("knight"), &err);
        const bool ok = !files.isEmpty();
        if (ok) ++exportOk;
        std::cout << "导出[" << AssetExporter::formatKey(fmt).toStdString() << "]: "
                  << files.size() << " 个文件" << (ok ? "" : " 失败: " + err.toStdString())
                  << std::endl;
        for (const QString& f : files)
            std::cout << "    " << QDir::toNativeSeparators(f).toStdString() << std::endl;
    }

    const bool ok = report.passed && !sheet.isNull() && exportOk == 4;
    std::cout << (ok ? "[PASS] 全流程测试通过（含四种导出格式）"
                     : "[DONE] 全流程完成（有人工确认项）") << std::endl;
    QCoreApplication::exit(ok ? 0 : 1);
}

// 标准化某个方向最近一张 raw 候选并锁定
bool standardizeAndLock(const QString& direction)
{
    AssetRecord rec = g_store->loadAssetRecord(g_assetId);
    const AssetSpec spec = rec.lastSpec;
    QString rawPath;
    QString seedVersion;
    for (const AssetVersion& v : rec.versions) {
        if (v.direction == direction) {
            rawPath = g_store->absolutePath(v.filePath); // 取最后一个该方向的版本
            seedVersion = v.versionId;
        }
    }
    if (rawPath.isEmpty())
        return false;
    QImage raw(rawPath);
    if (raw.isNull())
        return false;
    // 统一母版色板（与 MainWindow 相同逻辑）
    QVector<QRgb> shared;
    if (spec.type == AssetType::Character) {
        if (!rec.palette.isEmpty()) {
            shared = rec.palette;
        } else if (!rec.masterVersionId.isEmpty()) {
            QImage masterRaw(g_store->absolutePath(rec.masterFilePath()));
            if (!masterRaw.isNull()) {
                rec.palette = AssetFactory::paletteFromRaw(masterRaw, spec);
                shared = rec.palette;
                std::cout << "  统一母版色板: " << rec.palette.size() << " 色" << std::endl;
            }
        }
    }
    const StandardizeResult r = AssetFactory::standardize(raw, spec, shared);
    if (r.frame.isNull())
        return false;

    const QString versionId = newId(QStringLiteral("v"));
    const QString rel = g_store->saveProcessedImage(g_assetId, versionId, r.frame, direction);
    AssetVersion v;
    v.versionId = versionId;
    v.filePath = rel;
    v.kind = QStringLiteral("processed");
    v.direction = direction;
    v.model = QStringLiteral("asset-factory");
    v.created = QDateTime::currentDateTime();
    rec.versions.append(v);
    rec.lockedFrames.insert(direction, versionId);
    g_store->saveAssetRecord(rec);
    std::cout << "已锁定方向 " << direction.toStdString()
              << " (frame " << spec.frameSize << "x" << spec.frameSize
              << ", palette " << r.palette.size() << " 色)" << std::endl;
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("PipelineTest"));

    const QString root = QCoreApplication::applicationDirPath() + QStringLiteral("/test-projects");
    g_store = new ProjectStore(root, QStringLiteral("test"));
    QString err;
    if (!g_store->createOrOpen(&err)) {
        std::cout << "[FAIL] " << err.toStdString() << std::endl;
        return 2;
    }

    ModelRouter::Config cfg;
    cfg.mode = QStringLiteral("mock");
    TaskOrchestrator orch(g_store, cfg, &app);

    QObject::connect(&orch, &TaskOrchestrator::logMessage, [](const QString& l) {
        std::cout << "  [log] " << l.toStdString() << std::endl;
    });
    QObject::connect(&orch, &TaskOrchestrator::jobFinished, &orch, [&]() {
        AssetRecord rec = g_store->loadAssetRecord(g_assetId);
        const AssetSpec spec = rec.lastSpec;

        if (g_phase == 0) {
            // 第一轮结束：设定母版 → 引用母版生成其余方向
            std::cout << "--- 第一轮完成，设定母版 ---" << std::endl;
            QString masterVersion;
            for (const AssetVersion& v : rec.versions)
                if (v.direction == QStringLiteral("down"))
                    masterVersion = v.versionId;
            rec.masterVersionId = masterVersion;
            g_store->saveAssetRecord(rec);

            AssetSpec next = spec;
            next.id = newId(QStringLiteral("spec"));
            next.directions = { Direction::Up, Direction::Left, Direction::Right };
            next.referenceImages = QStringList{ g_store->absolutePath(rec.masterFilePath()) };
            g_store->saveSpec(g_assetId, next);
            g_phase = 1;
            orch.submit(next);
            return;
        }

        // 第二轮结束：锁定 down（母版）+ 新生成的三个方向 → 导出
        std::cout << "--- 第二轮完成，标准化并锁定各方向 ---" << std::endl;
        const QString dirs[4] = { QStringLiteral("down"), QStringLiteral("up"),
                                  QStringLiteral("left"), QStringLiteral("right") };
        for (const QString& d : dirs)
            if (!standardizeAndLock(d))
                std::cout << "  [warn] 方向 " << d.toStdString() << " 无候选，跳过" << std::endl;
        exportAndCheck();
    });

    // ---- 第一轮：正面（down）2 个候选 ----
    AssetRecord rec = g_store->createAsset(QStringLiteral("测试剑士"), AssetType::Character);
    g_assetId = rec.id;

    AssetSpec spec;
    spec.id = newId(QStringLiteral("spec"));
    spec.assetId = g_assetId;
    spec.type = AssetType::Character;
    spec.prompt = QStringLiteral("穿蓝色斗篷的少年剑士，银色短发，像素风");
    spec.negativePrompt = QStringLiteral("文字，水印");
    spec.directions = { Direction::Down };
    spec.candidateCount = 2;
    spec.frameSize = 64;
    spec.paletteSize = 32;
    g_store->saveSpec(g_assetId, spec);

    QTimer::singleShot(0, [&] { orch.submit(spec); });
    return app.exec();
}
