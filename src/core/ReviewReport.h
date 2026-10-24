#pragma once

// ReviewReport：程序检查 / 视觉审查 / 运行时测试的统一结果结构（技术路线 12）

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace gc {

struct ReviewIssue {
    QString id;       // 问题类型标识，如 "missing-frame"
    QString severity; // "error" | "warning"
    QString message;  // 人可读描述

    QJsonObject toJson() const
    {
        QJsonObject o;
        o.insert(QStringLiteral("id"), id);
        o.insert(QStringLiteral("severity"), severity);
        o.insert(QStringLiteral("message"), message);
        return o;
    }

    static ReviewIssue fromJson(const QJsonObject& o)
    {
        ReviewIssue i;
        i.id       = o.value(QStringLiteral("id")).toString();
        i.severity = o.value(QStringLiteral("severity")).toString();
        i.message  = o.value(QStringLiteral("message")).toString();
        return i;
    }
};

struct ReviewReport {
    QString versionId;         // 关联的导出版本；单帧检查时可为空
    double  score     = 0.0;   // 0~1，通过项加权占比
    bool    passed    = false; // 无 error 级问题即视为通过
    QString stage     = QStringLiteral("deterministic"); // deterministic | vision | sandbox
    QDateTime created;
    QVector<ReviewIssue> issues;

    QJsonObject toJson() const
    {
        QJsonObject o;
        o.insert(QStringLiteral("versionId"), versionId);
        o.insert(QStringLiteral("score"), score);
        o.insert(QStringLiteral("passed"), passed);
        o.insert(QStringLiteral("stage"), stage);
        o.insert(QStringLiteral("created"), created.toString(Qt::ISODateWithMs));
        QJsonArray a;
        for (const ReviewIssue& i : issues) a.append(i.toJson());
        o.insert(QStringLiteral("issues"), a);
        return o;
    }

    static ReviewReport fromJson(const QJsonObject& o)
    {
        ReviewReport r;
        r.versionId = o.value(QStringLiteral("versionId")).toString();
        r.score     = o.value(QStringLiteral("score")).toDouble();
        r.passed    = o.value(QStringLiteral("passed")).toBool();
        r.stage     = o.value(QStringLiteral("stage")).toString(QStringLiteral("deterministic"));
        r.created   = QDateTime::fromString(o.value(QStringLiteral("created")).toString(), Qt::ISODateWithMs);
        for (const QJsonValue& v : o.value(QStringLiteral("issues")).toArray())
            r.issues.append(ReviewIssue::fromJson(v.toObject()));
        return r;
    }

    QString toDisplayText() const
    {
        QString head = QStringLiteral("检查报告 [%1]  版本: %2\n得分: %3 / 1.00   结论: %4\n问题 (%5 条):")
                           .arg(stage, versionId.isEmpty() ? QStringLiteral("(未命名)") : versionId)
                           .arg(QString::number(score, 'f', 2))
                           .arg(passed ? QStringLiteral("通过") : QStringLiteral("未通过"))
                           .arg(issues.size());
        QString body;
        for (const ReviewIssue& i : issues) {
            body += QStringLiteral("\n  [%1] %2 — %3")
                        .arg(i.severity, i.id, i.message);
        }
        return head + body;
    }
};

} // namespace gc
