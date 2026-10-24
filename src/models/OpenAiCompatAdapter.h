#pragma once

// OpenAiCompatAdapter：云端生图适配器。
// 同时兼容 OpenAI images API 与硅基流动（SiliconFlow）：
//   OpenAI     请求 size，返回 data[].b64_json / data[].url
//   SiliconFlow 请求 image_size，返回 images[].url
// base_url + api_key + model 均在 config.json 中配置，密钥不写入源代码。

#include "models/IImageModel.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QVector>

namespace gc {

class OpenAiCompatAdapter : public IImageModel {
    Q_OBJECT
public:
    struct Config {
        QString baseUrl;   // 例：https://api.siliconflow.cn/v1
        QString apiKey;    // 优先读环境变量 GAMECREATOR_API_KEY
        QString model;     // 文生图模型，例：Tongyi-MAI/Z-Image-Turbo
        QString editModel; // 参考图编辑模型（有参考图时使用），例：Qwen/Qwen-Image-Edit-2509；留空则用 model
        int    timeoutMs = 180000;
    };

    explicit OpenAiCompatAdapter(const Config& cfg, QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("cloud(openai-compatible)"); }
    void generate(const GenerationRequest& req) override;
    void cancel() override;

private:
    QString assemblePrompt(const GenerationRequest& req) const;
    bool isSiliconFlow() const;
    QString editModelFor() const { return m_cfg.editModel.isEmpty() ? m_cfg.model : m_cfg.editModel; }
    QString imageSizeFor(const GenerationRequest& req) const;
    void requestGenerations(const GenerationRequest& req);
    void requestEdit(const GenerationRequest& req);
    // SiliconFlow 等网关无 /images/edits 端点：改用 /images/generations + image(base64) 参数
    void requestEditJson(const GenerationRequest& req);
    void downloadUrlImage(const QString& url);
    void handleReplyJson(QNetworkReply* reply, const QString& url);

    Config m_cfg;
    QNetworkAccessManager m_nam;
    QVector<QNetworkReply*> m_active;
};

} // namespace gc
