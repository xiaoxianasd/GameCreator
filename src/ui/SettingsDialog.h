#pragma once

// SettingsDialog：模型与 API 设置对话框。
// 密钥保存在本机 exe 目录的 config.json 中，不写入源代码、不随导出资产分发。

#include "models/ModelRouter.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;

namespace gc {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent, const ModelRouter::Config& cfg);

    ModelRouter::Config config() const;

private slots:
    void toggleKeyVisible(bool visible);

private:
    ModelRouter::Config m_base; // 进入对话框时的完整配置（保留 defaults 等未在界面展示的字段）
    QComboBox* m_modeCombo = nullptr;
    QLineEdit* m_baseUrlEdit = nullptr;
    QLineEdit* m_apiKeyEdit = nullptr;
    QLineEdit* m_modelEdit = nullptr;
    QLineEdit* m_editModelEdit = nullptr;
    QLineEdit* m_comfyUrlEdit = nullptr;
    QLineEdit* m_comfyWorkflowEdit = nullptr;
    QCheckBox* m_showKeyCheck = nullptr;
};

} // namespace gc
