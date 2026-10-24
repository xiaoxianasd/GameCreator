#pragma once

// MockAdapter：本地程序化生成适配器。
// 无需任何 API 密钥即可端到端验证整条流水线：
// 结构化规格 → 生成(高分辨率母版) → 背景去除 → 像素化 → 调色板量化 → 锚点 → 检查 → 导出。
// 真实 API 接入后将其切换为云端 / ComfyUI 适配器即可。

#include "models/IImageModel.h"

namespace gc {

class MockAdapter : public IImageModel {
    Q_OBJECT
public:
    explicit MockAdapter(QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("mock"); }
    void generate(const GenerationRequest& req) override;

private:
    QImage drawCharacter(const AssetSpec& spec, Direction dir, qlonglong seed,
                         const QColor& shirtRef) const;
    QImage drawItem(const AssetSpec& spec, qlonglong seed) const;
    QImage drawScene(const AssetSpec& spec, qlonglong seed) const;
    QColor referenceDominantColor(const QString& path) const;
};

} // namespace gc
