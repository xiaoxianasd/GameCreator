#include "models/OpenAiCompatAdapter.h"
#include "models/PromptPolicy.h"

#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace gc {

OpenAiCompatAdapter::OpenAiCompatAdapter(const Config& cfg, QObject* parent)
    : IImageModel(parent)
    , m_cfg(cfg)
{
    m_nam.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    if (m_cfg.apiKey.isEmpty())
        m_cfg.apiKey = qEnvironmentVariable("GAMECREATOR_API_KEY");
}

QString OpenAiCompatAdapter::assemblePrompt(const GenerationRequest& req) const
{
    // 结构化策略：生产规则 + StylePack + 用户描述 + 方向 VIEW + 一致性要求
    return PromptPolicy::compose(req.spec, req.direction);
}

bool OpenAiCompatAdapter::isSiliconFlow() const
{
    return m_cfg.baseUrl.contains(QLatin1String("siliconflow"), Qt::CaseInsensitive);
}

QString OpenAiCompatAdapter::imageSizeFor(const GenerationRequest& req) const
{
    const QString requested = QStringLiteral("%1x%2")
                                  .arg(req.spec.modelWidth)
                                  .arg(req.spec.modelHeight);
    if (!isSiliconFlow())
        return requested;

    // 硅基流动只接受预设尺寸；512 等自定义值会直接被拒绝。
    static const char* kPresets[] = {
        "1024x1024", "768x1024", "1024x768",
        "768x1344", "1344x768", "512x512", "576x1024", "1024x576"
    };
    for (const char* preset : kPresets) {
        if (requested == QLatin1String(preset))
            return requested;
    }

    const bool portrait = req.spec.modelHeight > req.spec.modelWidth;
    const bool landscape = req.spec.modelWidth > req.spec.modelHeight;
    if (portrait)
        return QStringLiteral("768x1024");
    if (landscape)
        return QStringLiteral("1024x768");
    return QStringLiteral("1024x1024");
}

void OpenAiCompatAdapter::generate(const GenerationRequest& req)
{
    if (m_cfg.baseUrl.isEmpty()) {
        emit requestFailed(QStringLiteral("云端模型未配置：请在 config.json 中填写 model.cloud.base_url"));
        return;
    }
    if (m_cfg.apiKey.isEmpty()) {
        emit requestFailed(QStringLiteral("云端模型未配置 API 密钥：请设置 config.json 的 api_key 或环境变量 GAMECREATOR_API_KEY"));
        return;
    }

    const bool useEdit = !req.spec.referenceImages.isEmpty();
    const bool editViaJson = useEdit && isSiliconFlow(); // SiliconFlow 无 /images/edits 端点
    emit statusMessage(QStringLiteral("云端模型：%1 → POST %2")
                           .arg(useEdit ? editModelFor() : m_cfg.model,
                                m_cfg.baseUrl
                                    + (useEdit
                                           ? (editViaJson
                                                  ? QStringLiteral("/images/generations (参考图)")
                                                  : QStringLiteral("/images/edits"))
                                           : QStringLiteral("/images/generations"))));

    if (useEdit) {
        if (editViaJson)
            requestEditJson(req);
        else
            requestEdit(req);
    } else {
        requestGenerations(req);
    }
}

void OpenAiCompatAdapter::requestGenerations(const GenerationRequest& req)
{
    const QString imageSize = imageSizeFor(req);
    QJsonObject body;
    body.insert(QStringLiteral("model"), m_cfg.model);
    body.insert(QStringLiteral("prompt"), assemblePrompt(req));
    if (isSiliconFlow()) {
        // 硅基流动：image_size + batch_size，返回 images[].url
        body.insert(QStringLiteral("image_size"), imageSize);
        body.insert(QStringLiteral("batch_size"), 1);
    } else {
        body.insert(QStringLiteral("n"), 1);
        body.insert(QStringLiteral("size"), imageSize);
    }
    // 负面清单：策略默认 + 用户补充。空值不发送，避免部分网关拒绝空字符串。
    QStringList neg = PromptPolicy::negative(req.spec.type)
                          .split(QStringLiteral(", "), Qt::SkipEmptyParts);
    if (!req.spec.negativePrompt.isEmpty())
        neg.append(req.spec.negativePrompt);
    if (!neg.isEmpty())
        body.insert(QStringLiteral("negative_prompt"), neg.join(QStringLiteral(", ")));
    // gpt-image 系列支持透明背景参数；硅基流动没有这个字段
    if (!isSiliconFlow()
        && m_cfg.model.contains(QLatin1String("gpt-image"), Qt::CaseInsensitive))
        body.insert(QStringLiteral("background"), QStringLiteral("transparent"));
    if (isSiliconFlow() && imageSize != QStringLiteral("%1x%2")
                                         .arg(req.spec.modelWidth)
                                         .arg(req.spec.modelHeight)) {
        emit statusMessage(QStringLiteral("硅基流动不支持 %1x%2，已改用预设尺寸 %3")
                               .arg(req.spec.modelWidth)
                               .arg(req.spec.modelHeight)
                               .arg(imageSize));
    }

    QNetworkRequest r(QUrl(m_cfg.baseUrl + QStringLiteral("/images/generations")));
    r.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    r.setRawHeader(QByteArrayLiteral("Authorization"),
                   QStringLiteral("Bearer %1").arg(m_cfg.apiKey).toUtf8());
    r.setTransferTimeout(m_cfg.timeoutMs);

    QNetworkReply* reply = m_nam.post(r, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_active.append(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_active.removeOne(reply);
        handleReplyJson(reply, QString());
        reply->deleteLater();
    });
}

void OpenAiCompatAdapter::requestEdit(const GenerationRequest& req)
{
    const QString imagePath = req.spec.referenceImages.first();
    QFile* file = new QFile(imagePath, this);
    if (!file->open(QIODevice::ReadOnly)) {
        emit requestFailed(QStringLiteral("无法读取参考图：%1").arg(imagePath));
        file->deleteLater();
        return;
    }

    QHttpMultiPart* multi = new QHttpMultiPart(QHttpMultiPart::FormDataType, this);
    auto addText = [&multi](const QString& name, const QString& value) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"%1\"").arg(name));
        part.setBody(value.toUtf8());
        multi->append(part);
    };
    addText(QStringLiteral("model"), editModelFor());
    addText(QStringLiteral("prompt"), assemblePrompt(req));
    addText(QStringLiteral("n"), QStringLiteral("1"));
    addText(QStringLiteral("size"),
            QStringLiteral("%1x%2").arg(req.spec.modelWidth).arg(req.spec.modelHeight));

    QHttpPart imgPart;
    const QString fileName = QFileInfo(imagePath).fileName();
    imgPart.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("image/png"));
    imgPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                      QStringLiteral("form-data; name=\"image\"; filename=\"%1\"").arg(fileName));
    imgPart.setBodyDevice(file);
    file->setParent(multi);
    multi->append(imgPart);

    QNetworkRequest r(QUrl(m_cfg.baseUrl + QStringLiteral("/images/edits")));
    r.setRawHeader(QByteArrayLiteral("Authorization"),
                   QStringLiteral("Bearer %1").arg(m_cfg.apiKey).toUtf8());
    r.setTransferTimeout(m_cfg.timeoutMs);

    QNetworkReply* reply = m_nam.post(r, multi);
    multi->setParent(reply); // 随 reply 释放
    m_active.append(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_active.removeOne(reply);
        handleReplyJson(reply, QString());
        reply->deleteLater();
    });
}

void OpenAiCompatAdapter::requestEditJson(const GenerationRequest& req)
{
    // SiliconFlow 参考图编辑：POST /images/generations，image 字段传 base64 data URI
    const QString imagePath = req.spec.referenceImages.first();
    QFile f(imagePath);
    if (!f.open(QIODevice::ReadOnly)) {
        emit requestFailed(QStringLiteral("无法读取参考图：%1").arg(imagePath));
        return;
    }
    const QByteArray png = f.readAll();
    f.close();
    if (png.isEmpty()) {
        emit requestFailed(QStringLiteral("参考图内容为空：%1").arg(imagePath));
        return;
    }
    if (png.size() > 10 * 1024 * 1024) {
        emit requestFailed(QStringLiteral("参考图过大（%1 MB > 10MB），请压缩后重试")
                               .arg(png.size() / 1024 / 1024));
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("model"), editModelFor());
    body.insert(QStringLiteral("prompt"), assemblePrompt(req));
    body.insert(QStringLiteral("image"),
                QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64()));
    body.insert(QStringLiteral("image_size"), imageSizeFor(req));
    body.insert(QStringLiteral("batch_size"), 1);

    QNetworkRequest r(QUrl(m_cfg.baseUrl + QStringLiteral("/images/generations")));
    r.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    r.setRawHeader(QByteArrayLiteral("Authorization"),
                   QStringLiteral("Bearer %1").arg(m_cfg.apiKey).toUtf8());
    r.setTransferTimeout(m_cfg.timeoutMs);

    QNetworkReply* reply = m_nam.post(r, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_active.append(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_active.removeOne(reply);
        handleReplyJson(reply, QString());
        reply->deleteLater();
    });
}

void OpenAiCompatAdapter::handleReplyJson(QNetworkReply* reply, const QString& url)
{
    if (reply->error() != QNetworkReply::NoError) {
        const int httpStatus = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString hint;
        if (httpStatus == 404)
            hint = QStringLiteral("\n提示：接口不存在（404）。该 base_url 没有 /images/generations 生图端点——"
                                  "DeepSeek 等「纯对话」API 无法用于生图，请换成支持生图的网关或本地 ComfyUI。");
        else if (httpStatus == 401 || httpStatus == 403)
            hint = QStringLiteral("\n提示：鉴权失败（%1），请检查 api_key 是否正确、是否开通生图权限。").arg(httpStatus);
        else if (httpStatus == 402)
            hint = QStringLiteral("\n提示：余额不足（402）。");
        else if (httpStatus >= 500)
            hint = QStringLiteral("\n提示：服务端错误（%1），请稍后重试。").arg(httpStatus);
        else if (httpStatus == 0)
            hint = QStringLiteral("\n提示：无法连接到服务器（网络不通、域名错误或被拦截）。");
        // 服务端错误信息优先展示（如 "Model disabled."），避免 403 一律误导为鉴权问题
        const QByteArray body = reply->readAll();
        QString bizMsg;
        {
            const QJsonObject bodyObj = QJsonDocument::fromJson(body).object();
            if (bodyObj.contains(QStringLiteral("message")))
                bizMsg = bodyObj.value(QStringLiteral("message")).toString();
        }
        emit requestFailed(QStringLiteral("云端请求失败（HTTP %1）：%2%3")
                               .arg(httpStatus > 0 ? QString::number(httpStatus)
                                                   : QString::number(reply->error()))
                               .arg(bizMsg.isEmpty() ? QString::fromUtf8(body.left(500))
                                                     : bizMsg,
                                    hint));
        if (httpStatus == 401 || httpStatus == 402 || httpStatus == 403 || httpStatus == 404)
            emit fatalError(QStringLiteral("HTTP %1：%2").arg(httpStatus).arg(hint.trimmed()));
        return;
    }
    const QByteArray data = reply->readAll();
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        emit requestFailed(QStringLiteral("云端返回非法 JSON：%1").arg(QString::fromUtf8(data.left(200))));
        return;
    }
    const QJsonObject root = doc.object();
    const QJsonValue errVal = root.value(QStringLiteral("error"));
    if (errVal.isObject()) {
        emit requestFailed(QStringLiteral("云端返回错误：%1")
                               .arg(errVal.toObject().value(QStringLiteral("message")).toString()));
        return;
    }
    // OpenAI 用 data[]，硅基流动用 images[]。两种都认，避免换网关后再改解析。
    QJsonArray arr = root.value(QStringLiteral("images")).toArray();
    if (arr.isEmpty())
        arr = root.value(QStringLiteral("data")).toArray();
    if (arr.isEmpty()) {
        emit requestFailed(QStringLiteral("云端返回为空：%1").arg(QString::fromUtf8(data.left(300))));
        return;
    }
    const QJsonObject first = arr.at(0).toObject();

    if (first.contains(QStringLiteral("b64_json"))) {
        const QByteArray png = QByteArray::fromBase64(
            first.value(QStringLiteral("b64_json")).toString().toLatin1());
        if (png.isEmpty()) {
            emit requestFailed(QStringLiteral("b64_json 解码为空"));
            return;
        }
        emit imageReady(png, 0);
    } else if (first.contains(QStringLiteral("url"))) {
        const QString imgUrl = first.value(QStringLiteral("url")).toString();
        if (url == imgUrl) { // 防循环
            emit requestFailed(QStringLiteral("图片 URL 下载循环：%1").arg(imgUrl));
            return;
        }
        downloadUrlImage(imgUrl);
    } else {
        emit requestFailed(QStringLiteral("云端返回缺少 b64_json/url 字段"));
    }
}

void OpenAiCompatAdapter::downloadUrlImage(const QString& url)
{
    QNetworkRequest r{ QUrl(url) };
    r.setTransferTimeout(m_cfg.timeoutMs);
    QNetworkReply* reply = m_nam.get(r);
    m_active.append(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
        m_active.removeOne(reply);
        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(QStringLiteral("图片下载失败：%1").arg(reply->errorString()));
        } else {
            const QByteArray png = reply->readAll();
            if (QImage::fromData(png, "PNG").isNull()
                && QImage::fromData(png).isNull()) {
                // 非 PNG：尝试按 JSON 容器再解析一次
                handleReplyJson(reply, url);
            } else {
                emit imageReady(png, 0);
            }
        }
        reply->deleteLater();
    });
}

void OpenAiCompatAdapter::cancel()
{
    for (QNetworkReply* r : m_active)
        r->abort();
    m_active.clear();
}

} // namespace gc
