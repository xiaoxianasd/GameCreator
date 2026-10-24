#include "models/ComfyUiAdapter.h"
#include "models/PromptPolicy.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QUrlQuery>

namespace gc {

ComfyUiAdapter::ComfyUiAdapter(const Config& cfg, QObject* parent)
    : IImageModel(parent)
    , m_cfg(cfg)
{
    connect(&m_pollTimer, &QTimer::timeout, this, &ComfyUiAdapter::pollHistory);
}

bool ComfyUiAdapter::loadWorkflow(QJsonObject& out, QString& err) const
{
    QString path = m_cfg.workflowPath;
    if (QFileInfo(path).isRelative())
        path = QCoreApplication::applicationDirPath() + QLatin1Char('/') + path;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        err = QStringLiteral("无法打开 ComfyUI 工作流模板：%1").arg(path);
        return false;
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        err = QStringLiteral("工作流模板不是合法 JSON 对象：%1").arg(path);
        return false;
    }
    out = doc.object();
    return true;
}

QJsonObject ComfyUiAdapter::substitute(const QJsonObject& node, const QHash<QString, QString>& vars)
{
    auto placeholder = [](const QString& key) {
        return QStringLiteral("{{") + key + QStringLiteral("}}");
    };
    QJsonObject result = node;
    for (auto it = result.begin(); it != result.end(); ++it) {
        const QJsonValue& v = it.value();
        if (v.isString()) {
            QString s = v.toString();
            for (auto vit = vars.constBegin(); vit != vars.constEnd(); ++vit)
                s.replace(placeholder(vit.key()), vit.value());
            result[it.key()] = s;
        } else if (v.isDouble() || v.isBool()) {
            // 数值/布尔也支持占位符（先转字符串替换再还原类型）
            QString s;
            if (v.isDouble()) s = QString::number(v.toDouble());
            else              s = v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
            bool changed = false;
            for (auto vit = vars.constBegin(); vit != vars.constEnd(); ++vit) {
                if (s.contains(placeholder(vit.key()))) {
                    s.replace(placeholder(vit.key()), vit.value());
                    changed = true;
                }
            }
            if (changed) {
                bool okNum = false;
                const double num = s.toDouble(&okNum);
                if (okNum) result[it.key()] = num;
                else if (s == QStringLiteral("true"))  result[it.key()] = true;
                else if (s == QStringLiteral("false")) result[it.key()] = false;
                else result[it.key()] = s;
            }
        } else if (v.isObject()) {
            result[it.key()] = substitute(v.toObject(), vars);
        } else if (v.isArray()) {
            QJsonArray arr = v.toArray();
            for (int i = 0; i < arr.size(); ++i) {
                if (arr.at(i).isObject())
                    arr[i] = substitute(arr.at(i).toObject(), vars);
            }
            result[it.key()] = arr;
        }
    }
    return result;
}

void ComfyUiAdapter::generate(const GenerationRequest& req)
{
    if (m_cfg.baseUrl.isEmpty()) {
        emit requestFailed(QStringLiteral("ComfyUI 未配置：请在 config.json 中填写 model.comfyui.base_url"));
        return;
    }

    QJsonObject workflow;
    QString err;
    if (!loadWorkflow(workflow, err)) {
        emit requestFailed(err);
        return;
    }

    // 结构化提示词策略（与云端适配器一致）
    const QString prompt = PromptPolicy::compose(req.spec, req.direction);
    QStringList neg = PromptPolicy::negative(req.spec.type)
                          .split(QStringLiteral(", "), Qt::SkipEmptyParts);
    if (!req.spec.negativePrompt.isEmpty())
        neg.append(req.spec.negativePrompt);

    // 生成参数暂存到内部键，上传完参考图后统一替换提交
    m_workflow = workflow;
    m_workflow.insert(QStringLiteral("__gc_prompt"), prompt);
    m_workflow.insert(QStringLiteral("__gc_negative"), neg.join(QStringLiteral(", ")));
    m_workflow.insert(QStringLiteral("__gc_seed"),
                      QString::number(QRandomGenerator::global()->bounded(1, 2147483000)));
    m_workflow.insert(QStringLiteral("__gc_width"),  QString::number(req.spec.modelWidth));
    m_workflow.insert(QStringLiteral("__gc_height"), QString::number(req.spec.modelHeight));

    // 参考图队列：先全部上传，再提交工作流
    m_pendingRefs = req.spec.referenceImages;
    m_uploadedNames.clear();
    m_cancelled = false;
    m_pollCount = 0;

    emit statusMessage(QStringLiteral("ComfyUI：%1")
                           .arg(m_pendingRefs.isEmpty()
                                    ? QStringLiteral("提交工作流…")
                                    : QStringLiteral("上传 %1 张参考图…").arg(m_pendingRefs.size())));
    uploadNextReference();
}

void ComfyUiAdapter::uploadNextReference()
{
    if (m_cancelled)
        return;
    if (m_pendingRefs.isEmpty()) {
        submitWorkflow();
        return;
    }
    const QString path = m_pendingRefs.takeFirst();
    QFile* file = new QFile(path, this);
    if (!file->open(QIODevice::ReadOnly)) {
        emit requestFailed(QStringLiteral("无法读取参考图：%1").arg(path));
        file->deleteLater();
        return;
    }

    QHttpMultiPart* multi = new QHttpMultiPart(QHttpMultiPart::FormDataType, this);
    QHttpPart imgPart;
    imgPart.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("image/png"));
    imgPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                      QStringLiteral("form-data; name=\"image\"; filename=\"%1\"")
                          .arg(QFileInfo(path).fileName()));
    imgPart.setBodyDevice(file);
    file->setParent(multi);
    multi->append(imgPart);

    QNetworkRequest r(QUrl(m_cfg.baseUrl + QStringLiteral("/upload/image")));
    r.setTransferTimeout(60000);
    QNetworkReply* reply = m_nam.post(r, multi);
    multi->setParent(reply); // 随 reply 释放
    connect(reply, &QNetworkReply::finished, this, [this, reply, path] {
        reply->deleteLater();
        if (m_cancelled)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(QStringLiteral("参考图上传失败（%1）：%2")
                                   .arg(path, reply->errorString()));
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const QString name = root.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) {
            emit requestFailed(QStringLiteral("ComfyUI 上传返回缺少文件名（%1）").arg(path));
            return;
        }
        m_uploadedNames.append(name);
        uploadNextReference();
    });
}

void ComfyUiAdapter::submitWorkflow()
{
    // 取回暂存参数，完成占位符替换后提交
    const QString prompt = m_workflow.take(QStringLiteral("__gc_prompt")).toString();
    const QString negative = m_workflow.take(QStringLiteral("__gc_negative")).toString();
    const QString seed = m_workflow.take(QStringLiteral("__gc_seed")).toString();
    const QString width = m_workflow.take(QStringLiteral("__gc_width")).toString();
    const QString height = m_workflow.take(QStringLiteral("__gc_height")).toString();

    QHash<QString, QString> vars;
    vars.insert(QStringLiteral("PROMPT"), prompt);
    vars.insert(QStringLiteral("NEGATIVE"), negative);
    vars.insert(QStringLiteral("SEED"), seed);
    vars.insert(QStringLiteral("WIDTH"), width);
    vars.insert(QStringLiteral("HEIGHT"), height);
    if (!m_uploadedNames.isEmpty()) {
        vars.insert(QStringLiteral("REFERENCE_IMAGE"), m_uploadedNames.first());
        for (int i = 0; i < m_uploadedNames.size(); ++i)
            vars.insert(QStringLiteral("REFERENCE_IMAGE_%1").arg(i + 1), m_uploadedNames[i]);
    }

    QJsonObject body;
    body.insert(QStringLiteral("prompt"), substitute(m_workflow, vars));
    body.insert(QStringLiteral("client_id"), QStringLiteral("gamecreator"));

    QNetworkRequest r(QUrl(m_cfg.baseUrl + QStringLiteral("/prompt")));
    r.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply* reply = m_nam.post(r, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (m_cancelled)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(QStringLiteral("ComfyUI 提交失败：%1（服务是否已启动？）")
                                   .arg(reply->errorString()));
            return;
        }
        const QByteArray raw = reply->readAll();
        const QJsonObject root = QJsonDocument::fromJson(raw).object();
        if (root.contains(QStringLiteral("error"))) {
            emit requestFailed(QStringLiteral("ComfyUI 工作流错误：%1")
                                   .arg(QString::fromUtf8(raw.left(400))));
            return;
        }
        m_promptId = root.value(QStringLiteral("prompt_id")).toString();
        if (m_promptId.isEmpty()) {
            emit requestFailed(QStringLiteral("ComfyUI 未返回 prompt_id"));
            return;
        }
        m_pollTimer.start(m_cfg.pollMs);
    });
}

void ComfyUiAdapter::pollHistory()
{
    if (m_cancelled || m_promptId.isEmpty())
        return;
    if (++m_pollCount > m_cfg.maxPolls) {
        m_pollTimer.stop();
        emit requestFailed(QStringLiteral("ComfyUI 生成超时（%1 次轮询）").arg(m_cfg.maxPolls));
        return;
    }

    QNetworkRequest r(QUrl(m_cfg.baseUrl + QStringLiteral("/history/") + m_promptId));
    QNetworkReply* reply = m_nam.get(r);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (m_cancelled)
            return;
        if (reply->error() != QNetworkReply::NoError)
            return; // 网络抖动：下一轮继续

        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonObject entry = root.value(m_promptId).toObject();
        if (entry.isEmpty())
            return; // 尚未出现在历史 → 继续轮询

        const QJsonObject status = entry.value(QStringLiteral("status")).toObject();
        if (!status.value(QStringLiteral("completed")).toBool())
            return;

        m_pollTimer.stop();
        collectImages(entry);
    });
}

void ComfyUiAdapter::collectImages(const QJsonObject& historyEntry)
{
    const QJsonObject outputs = historyEntry.value(QStringLiteral("outputs")).toObject();
    bool any = false;
    for (auto it = outputs.begin(); it != outputs.end(); ++it) {
        const QJsonArray images = it.value().toObject().value(QStringLiteral("images")).toArray();
        for (const QJsonValue& v : images) {
            const QJsonObject img = v.toObject();
            const QString fileName = img.value(QStringLiteral("filename")).toString();
            const QString subfolder = img.value(QStringLiteral("subfolder")).toString();
            const QString type = img.value(QStringLiteral("type")).toString(QStringLiteral("output"));

            QUrlQuery q;
            q.addQueryItem(QStringLiteral("filename"), fileName);
            q.addQueryItem(QStringLiteral("subfolder"), subfolder);
            q.addQueryItem(QStringLiteral("type"), type);
            QNetworkRequest r(QUrl(m_cfg.baseUrl + QStringLiteral("/view?") + q.toString(QUrl::FullyEncoded)));
            QNetworkReply* reply = m_nam.get(r);
            any = true;
            connect(reply, &QNetworkReply::finished, this, [this, reply] {
                reply->deleteLater();
                if (reply->error() != QNetworkReply::NoError) {
                    emit requestFailed(QStringLiteral("ComfyUI 图片拉取失败：%1").arg(reply->errorString()));
                    return;
                }
                emit imageReady(reply->readAll(), 0);
            });
        }
    }
    if (!any)
        emit requestFailed(QStringLiteral("ComfyUI 任务完成但没有输出图片"));
}

void ComfyUiAdapter::cancel()
{
    m_cancelled = true;
    m_pollTimer.stop();
}

} // namespace gc
