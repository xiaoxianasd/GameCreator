#pragma once

// ModelRouter：模型路由与配置装载（技术路线 6.3）。
// 模式：
//   mock   —— 离线程序化生成（默认，全流程验证）
//   cloud  —— OpenAI 兼容云端 API
//   comfyui—— 本地 ComfyUI
//   auto   —— 云端已配置 key → 云端；否则 ComfyUI 工作流存在 → ComfyUI；否则 mock

#include "core/AssetSpec.h"
#include "models/ComfyUiAdapter.h"
#include "models/IImageModel.h"
#include "models/MockAdapter.h"
#include "models/OpenAiCompatAdapter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace gc {

class ModelRouter {
public:
    struct Config {
        // auto：云端已配置 → 云端；否则 ComfyUI 工作流存在 → ComfyUI；否则离线 mock
        QString mode = QStringLiteral("auto");
        OpenAiCompatAdapter::Config cloud;
        ComfyUiAdapter::Config comfy;
        // 默认素材规格
        int frameSize = 64;
        int paletteSize = 32;
        int candidates = 4;
    };

    static QString defaultConfigPath()
    {
        return QCoreApplication::applicationDirPath() + QStringLiteral("/config.json");
    }

    // 读取 config.json；文件不存在时返回默认（mock 模式）并可选写出默认文件
    static Config load(const QString& path, bool writeDefaultIfMissing = true)
    {
        Config cfg;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            if (writeDefaultIfMissing)
                writeDefault(path);
            return cfg;
        }
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError)
            return cfg;
        const QJsonObject root = doc.object();

        const QJsonObject model = root.value(QStringLiteral("model")).toObject();
        cfg.mode = model.value(QStringLiteral("mode")).toString(QStringLiteral("mock"));

        const QJsonObject cloud = model.value(QStringLiteral("cloud")).toObject();
        cfg.cloud.baseUrl = cloud.value(QStringLiteral("base_url")).toString();
        cfg.cloud.apiKey  = cloud.value(QStringLiteral("api_key")).toString();
        cfg.cloud.model   = cloud.value(QStringLiteral("model"))
                                .toString(QStringLiteral("Tongyi-MAI/Z-Image-Turbo"));
        cfg.cloud.editModel = cloud.value(QStringLiteral("edit_model")).toString();
        cfg.cloud.timeoutMs = cloud.value(QStringLiteral("timeout_ms")).toInt(180000);

        const QJsonObject comfy = model.value(QStringLiteral("comfyui")).toObject();
        cfg.comfy.baseUrl = comfy.value(QStringLiteral("base_url")).toString(QStringLiteral("http://127.0.0.1:8188"));
        cfg.comfy.workflowPath = comfy.value(QStringLiteral("workflow")).toString(QStringLiteral("workflows/comfyui_example.json"));
        cfg.comfy.pollMs = comfy.value(QStringLiteral("poll_ms")).toInt(1000);
        cfg.comfy.maxPolls = comfy.value(QStringLiteral("max_polls")).toInt(180);

        const QJsonObject defs = root.value(QStringLiteral("defaults")).toObject();
        cfg.frameSize   = defs.value(QStringLiteral("frame_size")).toInt(64);
        cfg.paletteSize = defs.value(QStringLiteral("palette_size")).toInt(32);
        cfg.candidates  = defs.value(QStringLiteral("candidates")).toInt(4);
        return cfg;
    }

    static void writeDefault(const QString& path)
    {
        QJsonObject cloud, comfy, model, defaults, root;
        cloud.insert(QStringLiteral("base_url"), QStringLiteral("https://api.siliconflow.cn/v1"));
        cloud.insert(QStringLiteral("api_key"), QString());
        cloud.insert(QStringLiteral("model"), QStringLiteral("Tongyi-MAI/Z-Image-Turbo"));
        cloud.insert(QStringLiteral("edit_model"), QStringLiteral("Qwen/Qwen-Image-Edit-2509"));
        comfy.insert(QStringLiteral("base_url"), QStringLiteral("http://127.0.0.1:8188"));
        comfy.insert(QStringLiteral("workflow"), QStringLiteral("workflows/comfyui_example.json"));
        model.insert(QStringLiteral("mode"), QStringLiteral("auto"));
        model.insert(QStringLiteral("cloud"), cloud);
        model.insert(QStringLiteral("comfyui"), comfy);
        defaults.insert(QStringLiteral("frame_size"), 64);
        defaults.insert(QStringLiteral("palette_size"), 32);
        defaults.insert(QStringLiteral("candidates"), 4);
        root.insert(QStringLiteral("model"), model);
        root.insert(QStringLiteral("defaults"), defaults);

        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }

    // 写回 config.json（保留 defaults 等全部字段）
    static bool save(const QString& path, const Config& cfg)
    {
        QJsonObject cloud, comfy, model, defaults, root;
        cloud.insert(QStringLiteral("base_url"), cfg.cloud.baseUrl);
        cloud.insert(QStringLiteral("api_key"), cfg.cloud.apiKey);
        cloud.insert(QStringLiteral("model"), cfg.cloud.model);
        cloud.insert(QStringLiteral("edit_model"), cfg.cloud.editModel);
        cloud.insert(QStringLiteral("timeout_ms"), cfg.cloud.timeoutMs);
        comfy.insert(QStringLiteral("base_url"), cfg.comfy.baseUrl);
        comfy.insert(QStringLiteral("workflow"), cfg.comfy.workflowPath);
        model.insert(QStringLiteral("mode"), cfg.mode);
        model.insert(QStringLiteral("cloud"), cloud);
        model.insert(QStringLiteral("comfyui"), comfy);
        defaults.insert(QStringLiteral("frame_size"), cfg.frameSize);
        defaults.insert(QStringLiteral("palette_size"), cfg.paletteSize);
        defaults.insert(QStringLiteral("candidates"), cfg.candidates);
        root.insert(QStringLiteral("model"), model);
        root.insert(QStringLiteral("defaults"), defaults);

        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        return true;
    }

    // 解析生效模式（auto 的回退逻辑）
    static QString resolveMode(const Config& cfg)
    {
        const QString mode = cfg.mode.toLower();
        if (mode == QLatin1String("cloud") || mode == QLatin1String("mock") || mode == QLatin1String("comfyui"))
            return mode;
        // auto：云端优先 → ComfyUI → mock
        if (!cfg.cloud.baseUrl.isEmpty() && (!cfg.cloud.apiKey.isEmpty() || !qEnvironmentVariableIsEmpty("GAMECREATOR_API_KEY")))
            return QStringLiteral("cloud");
        QString wf = cfg.comfy.workflowPath;
        if (QFileInfo(wf).isRelative())
            wf = QCoreApplication::applicationDirPath() + QLatin1Char('/') + wf;
        if (!cfg.comfy.baseUrl.isEmpty() && QFileInfo::exists(wf))
            return QStringLiteral("comfyui");
        return QStringLiteral("mock");
    }

    // 创建适配器（调用者负责持有 / 释放）
    static IImageModel* createAdapter(const Config& cfg, QObject* parent)
    {
        const QString mode = resolveMode(cfg);
        if (mode == QLatin1String("cloud"))
            return new OpenAiCompatAdapter(cfg.cloud, parent);
        if (mode == QLatin1String("comfyui"))
            return new ComfyUiAdapter(cfg.comfy, parent);
        return new MockAdapter(parent);
    }

    static QString modeToDisplay(const QString& mode)
    {
        if (mode == QLatin1String("cloud"))   return QStringLiteral("云端 API");
        if (mode == QLatin1String("comfyui")) return QStringLiteral("本地 ComfyUI");
        if (mode == QLatin1String("auto"))    return QStringLiteral("自动");
        return QStringLiteral("离线模拟");
    }
};

} // namespace gc
