#pragma once

// TaskOrchestrator：任务编排器（技术路线 5.3）。
// MVP 采用可控状态机（而非自由运行 Agent）：
//   Idle → Generating → (串行队列逐张回调) → Finished / Cancelled / Failed
// 职责：按「方向 × 候选数」拆分任务、选择适配器、保存原始输出与版本记录、
//       统计成功/失败、支持取消。所有生成结果产生新版本，不覆盖旧版本。
// 说明：MVP 采用串行提交（一次一张），保证异步适配器返回结果与方向正确对应；
//       云端批量并发可在此层之上扩展。

#include "core/AssetSpec.h"
#include "models/ModelRouter.h"
#include "storage/ProjectStore.h"

#include <QPair>
#include <QObject>
#include <QVector>

namespace gc {

class TaskOrchestrator : public QObject {
    Q_OBJECT
public:
    explicit TaskOrchestrator(ProjectStore* store, const ModelRouter::Config& cfg,
                              QObject* parent = nullptr);

    bool busy() const { return m_adapter != nullptr; }

    // 运行中更新配置（设置对话框保存后调用；只影响下一个任务）
    void setConfig(const ModelRouter::Config& cfg) { m_cfg = cfg; }

    // 提交一个生成任务（spec.assetId 必须已创建）
    void submit(const AssetSpec& spec);
    void cancel();

signals:
    void logMessage(const QString& line);
    // 每张候选图就绪（rawPath 为相对项目目录的 raw 文件路径）
    void candidateReady(const QString& assetId, const QString& versionId,
                        const QString& direction, const QString& rawPath,
                        qlonglong seed, const QString& model);
    void jobFinished(const QString& assetId, int okCount, int failCount);

private:
    struct JobContext {
        AssetSpec spec;
        int expected = 0;
        int okCount = 0;
        int failCount = 0;
        bool cancelled = false;
        Direction currentDir = Direction::Down;
    };

    void submitNext();
    void finishJob();

    ProjectStore* m_store = nullptr;
    ModelRouter::Config m_cfg;
    IImageModel* m_adapter = nullptr;
    JobContext m_job;
    QVector<QPair<Direction, int>> m_queue; // 待提交的 (方向, 候选索引)
};

} // namespace gc
