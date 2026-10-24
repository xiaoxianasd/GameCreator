#pragma once

// IImageModel：统一模型适配层接口（技术路线 5.4）
// C++ 主程序只依赖本接口，不直接依赖任何模型供应商。
// 具体实现：
//   MockAdapter         —— 本地程序化生成（离线验证全流程）
//   OpenAiCompatAdapter —— 云端 OpenAI 兼容 images API
//   ComfyUiAdapter      —— 本地 ComfyUI 工作流服务

#include "core/AssetSpec.h"

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace gc {

// 模型返回的单张图片
struct ModelImage {
    QByteArray pngData;  // 完整 PNG 字节
    qlonglong  seed = 0; // 随机种子（若模型返回）
    QVariantMap meta;    // 适配器附加元数据（模型名、费用等）

    QImage decode() const
    {
        QImage img;
        if (!pngData.isEmpty())
            img = QImage::fromData(pngData, "PNG");
        return img;
    }
};

// 单次生成请求：一个方向的一个候选
struct GenerationRequest {
    AssetSpec spec;
    Direction direction = Direction::Down;
    int candidateIndex = 0;
};

class IImageModel : public QObject {
    Q_OBJECT
public:
    explicit IImageModel(QObject* parent = nullptr)
        : QObject(parent) {}
    ~IImageModel() override = default;

    virtual QString name() const = 0;
    virtual void generate(const GenerationRequest& req) = 0;
    virtual void cancel() {}

signals:
    // 信号只用基础类型，避免跨线程元类型注册问题
    void imageReady(const QByteArray& pngData, qlonglong seed);
    void requestFailed(const QString& error);
    // 致命错误（鉴权失败 / 接口不存在 / 余额不足等）：编排器应立即中止整个任务
    void fatalError(const QString& error);
    void statusMessage(const QString& msg);
};

} // namespace gc
