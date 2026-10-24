#include "ui/SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace gc {

SettingsDialog::SettingsDialog(QWidget* parent, const ModelRouter::Config& cfg)
    : QDialog(parent)
    , m_base(cfg)
{
    setWindowTitle(QStringLiteral("模型与 API 设置"));
    setMinimumWidth(560);

    auto* form = new QFormLayout;

    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem(QStringLiteral("自动（有密钥走云端，否则 ComfyUI/离线）"), QStringLiteral("auto"));
    m_modeCombo->addItem(QStringLiteral("云端 API"), QStringLiteral("cloud"));
    m_modeCombo->addItem(QStringLiteral("本地 ComfyUI"), QStringLiteral("comfyui"));
    m_modeCombo->addItem(QStringLiteral("离线模拟（无需 API）"), QStringLiteral("mock"));
    {
        const int idx = m_modeCombo->findData(cfg.mode);
        m_modeCombo->setCurrentIndex(idx < 0 ? 0 : idx);
    }
    form->addRow(QStringLiteral("生成模式"), m_modeCombo);

    m_baseUrlEdit = new QLineEdit(cfg.cloud.baseUrl, this);
    m_baseUrlEdit->setPlaceholderText(QStringLiteral("https://api.siliconflow.cn/v1"));
    form->addRow(QStringLiteral("云端 base_url"), m_baseUrlEdit);

    m_apiKeyEdit = new QLineEdit(cfg.cloud.apiKey, this);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText(QStringLiteral("sk-...（留空则用环境变量 GAMECREATOR_API_KEY）"));
    auto* keyRow = new QHBoxLayout;
    keyRow->addWidget(m_apiKeyEdit, 1);
    m_showKeyCheck = new QCheckBox(QStringLiteral("显示"), this);
    keyRow->addWidget(m_showKeyCheck);
    connect(m_showKeyCheck, &QCheckBox::toggled, this, &SettingsDialog::toggleKeyVisible);
    form->addRow(QStringLiteral("API 密钥"), keyRow);

    m_modelEdit = new QLineEdit(cfg.cloud.model, this);
    m_modelEdit->setPlaceholderText(QStringLiteral("Tongyi-MAI/Z-Image-Turbo"));
    form->addRow(QStringLiteral("文生图模型"), m_modelEdit);

    m_editModelEdit = new QLineEdit(cfg.cloud.editModel, this);
    m_editModelEdit->setPlaceholderText(QStringLiteral("Qwen/Qwen-Image-Edit-2509（参考图/四方向用）"));
    form->addRow(QStringLiteral("参考图编辑模型"), m_editModelEdit);

    m_comfyUrlEdit = new QLineEdit(cfg.comfy.baseUrl, this);
    m_comfyUrlEdit->setPlaceholderText(QStringLiteral("http://127.0.0.1:8188"));
    form->addRow(QStringLiteral("ComfyUI 地址"), m_comfyUrlEdit);

    m_comfyWorkflowEdit = new QLineEdit(cfg.comfy.workflowPath, this);
    m_comfyWorkflowEdit->setPlaceholderText(QStringLiteral("workflows/comfyui_example.json"));
    form->addRow(QStringLiteral("ComfyUI 工作流模板"), m_comfyWorkflowEdit);

    auto* note = new QLabel(QStringLiteral(
        "提示：配置保存在本程序目录的 config.json（含密钥，请勿分享该文件）。\n"
        "切换/新增服务商时只需改 base_url + 模型名，程序适配 OpenAI 兼容与 SiliconFlow 两种方言。"),
        this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:#666;"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(note);
    root->addWidget(buttons);
}

void SettingsDialog::toggleKeyVisible(bool visible)
{
    m_apiKeyEdit->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
}

ModelRouter::Config SettingsDialog::config() const
{
    // 以进入对话框时的完整配置为底，只覆盖界面上的字段（保留 defaults 等）
    ModelRouter::Config cfg = m_base;
    cfg.mode = m_modeCombo->currentData().toString();
    cfg.cloud.baseUrl = m_baseUrlEdit->text().trimmed();
    cfg.cloud.apiKey = m_apiKeyEdit->text().trimmed();
    cfg.cloud.model = m_modelEdit->text().trimmed();
    cfg.cloud.editModel = m_editModelEdit->text().trimmed();
    cfg.comfy.baseUrl = m_comfyUrlEdit->text().trimmed();
    cfg.comfy.workflowPath = m_comfyWorkflowEdit->text().trimmed();
    return cfg;
}

} // namespace gc
