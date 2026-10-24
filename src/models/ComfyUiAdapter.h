#pragma once

// ComfyUiAdapter：本地 ComfyUI 服务适配器（技术路线 6.2）。
// ComfyUI 作为独立本地 GPU 服务运行；本适配器负责：
//   1. 读取预置工作流 JSON 模板（含 {{PLACEHOLDER}} 占位符）；
//   2. 上传参考图（母版/风格参考）到 ComfyUI（POST /upload/image）；
//   3. 替换提示词 / 负面词 / 种子 / 尺寸 / 参考图文件名；
//   4. POST /prompt 提交任务，轮询 /history/{id}，从 /view 拉取输出图片。
// 占位符：{{PROMPT}} {{NEGATIVE}} {{SEED}} {{WIDTH}} {{HEIGHT}}
//         {{REFERENCE_IMAGE}}（第 1 张参考图）{{REFERENCE_IMAGE_1..N}}

#include "models/IImageModel.h"

#include <QNetworkAccessManager>
#include <QStringList>
#include <QTimer>

namespace gc {

class ComfyUiAdapter : public IImageModel {
    Q_OBJECT
public:
    struct Config {
        QString baseUrl;      // 例：http://127.0.0.1:8188
        QString workflowPath; // 工作流模板 JSON（相对程序目录或绝对路径）
        int    pollMs   = 1000;
        int    maxPolls = 180; // 最长等待 3 分钟
    };

    explicit ComfyUiAdapter(const Config& cfg, QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("comfyui"); }
    void generate(const GenerationRequest& req) override;
    void cancel() override;

private:
    bool loadWorkflow(QJsonObject& out, QString& err) const;
    static QJsonObject substitute(const QJsonObject& node, const QHash<QString, QString>& vars);
    void uploadNextReference();
    void submitWorkflow();
    void pollHistory();
    void collectImages(const QJsonObject& historyEntry);

    Config m_cfg;
    QNetworkAccessManager m_nam;
    QTimer m_pollTimer;

    // 一次 generate 的执行上下文
    QJsonObject m_workflow;
    QStringList m_pendingRefs;    // 待上传参考图绝对路径
    QStringList m_uploadedNames;  // ComfyUI 返回的文件名
    QString m_promptId;
    int m_pollCount = 0;
    bool m_cancelled = false;
};

} // namespace gc
