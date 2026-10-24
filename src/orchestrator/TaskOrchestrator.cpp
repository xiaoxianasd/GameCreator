#include "orchestrator/TaskOrchestrator.h"

namespace gc {

TaskOrchestrator::TaskOrchestrator(ProjectStore* store, const ModelRouter::Config& cfg,
                                   QObject* parent)
    : QObject(parent)
    , m_store(store)
    , m_cfg(cfg)
{
}

void TaskOrchestrator::submit(const AssetSpec& spec)
{
    if (busy()) {
        emit logMessage(QStringLiteral("上一个任务尚未结束，请先取消或等待完成。"));
        return;
    }
    if (spec.directions.isEmpty()) {
        emit logMessage(QStringLiteral("任务被拒绝：没有选择任何方向。"));
        return;
    }

    m_job = JobContext();
    m_job.spec = spec;

    // 任务拆分：角色按方向拆分，物品/场景只有一个“方向”
    m_queue.clear();
    for (int i = 0; i < spec.candidateCount; ++i) {
        for (Direction d : spec.directions) {
            m_queue.append(qMakePair(d, i));
        }
    }
    m_job.expected = m_queue.size();

    m_adapter = ModelRouter::createAdapter(m_cfg, this);
    const QString mode = ModelRouter::resolveMode(m_cfg);
    emit logMessage(QStringLiteral("任务开始：%1 | 适配器: %2 | 方向数: %3 | 每方向候选: %4 | 共 %5 张")
                        .arg(assetTypeToDisplay(spec.type),
                             ModelRouter::modeToDisplay(mode))
                        .arg(spec.type == AssetType::Character
                                 ? QString::number(spec.directions.size()) : QStringLiteral("1"))
                        .arg(spec.candidateCount)
                        .arg(m_job.expected));

    connect(m_adapter, &IImageModel::imageReady, this,
            [this](const QByteArray& png, qlonglong seed) {
                if (m_job.cancelled || png.isEmpty())
                    return;
                const QString dirKey = m_job.spec.type == AssetType::Character
                                           ? directionToString(m_job.currentDir) : QString();

                // 保存原始输出（永不覆盖）+ 版本记录
                const QString versionId = newId(QStringLiteral("v"));
                const QString rawRel = m_store->saveRawImage(m_job.spec.assetId, versionId,
                                                             png, dirKey);
                if (!rawRel.isEmpty()) {
                    AssetRecord rec = m_store->loadAssetRecord(m_job.spec.assetId);
                    AssetVersion v;
                    v.versionId = versionId;
                    v.filePath = rawRel;
                    v.kind = QStringLiteral("raw");
                    v.direction = dirKey;
                    v.model = m_adapter->name();
                    v.seed = seed;
                    v.created = QDateTime::currentDateTime();
                    rec.versions.append(v);
                    rec.lastSpec = m_job.spec;
                    m_store->saveAssetRecord(rec);

                    ++m_job.okCount;
                    emit candidateReady(m_job.spec.assetId, versionId, dirKey, rawRel, seed,
                                        m_adapter->name());
                } else {
                    ++m_job.failCount;
                    emit logMessage(QStringLiteral("候选图保存失败（磁盘写入错误）。"));
                }
                submitNext();
            });

    connect(m_adapter, &IImageModel::requestFailed, this, [this](const QString& error) {
        if (m_job.cancelled)
            return;
        ++m_job.failCount;
        emit logMessage(QStringLiteral("生成失败（%1/%2）：%3")
                            .arg(m_job.failCount).arg(m_job.expected).arg(error));
        submitNext();
    });

    // 致命错误（401/404/余额不足等）：立即中止整个任务，不再继续烧请求
    connect(m_adapter, &IImageModel::fatalError, this, [this](const QString& error) {
        if (m_job.cancelled || !m_adapter)
            return;
        m_job.cancelled = true;
        m_adapter->cancel();
        emit logMessage(QStringLiteral("任务中止（致命错误）：%1").arg(error));
        finishJob();
    });

    connect(m_adapter, &IImageModel::statusMessage, this,
            [this](const QString& msg) { emit logMessage(msg); });

    submitNext();
}

void TaskOrchestrator::submitNext()
{
    if (m_job.cancelled || !m_adapter)
        return;
    if (m_queue.isEmpty()) {
        finishJob();
        return;
    }
    const QPair<Direction, int> next = m_queue.takeFirst();
    m_job.currentDir = next.first;

    GenerationRequest req;
    req.spec = m_job.spec;
    req.direction = m_job.spec.type == AssetType::Character ? next.first : Direction::Down;
    req.candidateIndex = next.second;
    m_adapter->generate(req);
}

void TaskOrchestrator::finishJob()
{
    const QString assetId = m_job.spec.assetId;
    const int ok = m_job.okCount;
    const int fail = m_job.failCount;
    emit logMessage(QStringLiteral("任务结束：成功 %1，失败 %2。%3")
                        .arg(ok).arg(fail)
                        .arg(ok > 0 ? QStringLiteral("请在右侧选择候选图进行后续处理。")
                                    : QStringLiteral("全部失败：请检查 config.json 配置或稍后重试。")));
    m_adapter->deleteLater();
    m_adapter = nullptr;
    emit jobFinished(assetId, ok, fail);
}

void TaskOrchestrator::cancel()
{
    if (!m_adapter)
        return;
    m_job.cancelled = true;
    m_adapter->cancel();
    emit logMessage(QStringLiteral("任务已取消（成功 %1，失败 %2）。")
                        .arg(m_job.okCount).arg(m_job.failCount));
    m_adapter->deleteLater();
    m_adapter = nullptr;
    emit jobFinished(m_job.spec.assetId, m_job.okCount, m_job.failCount);
}

} // namespace gc
