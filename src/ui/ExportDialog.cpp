#include "ui/ExportDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace gc {

ExportDialog::ExportDialog(QWidget* parent, const QString& defaultBaseName)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("导出素材"));
    setMinimumWidth(520);

    auto* form = new QFormLayout;

    m_formatCombo = new QComboBox(this);
    for (AssetExporter::Format f : AssetExporter::allFormats())
        m_formatCombo->addItem(AssetExporter::formatName(f),
                               AssetExporter::formatKey(f));
    form->addRow(QStringLiteral("导出格式"), m_formatCombo);

    m_dirEdit = new QLineEdit(this);
    QSettings settings;
    QString lastDir = settings.value(QStringLiteral("export/dir")).toString();
    if (lastDir.isEmpty() || !QDir(lastDir).exists())
        lastDir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    m_dirEdit->setText(QDir::toNativeSeparators(lastDir));
    auto* dirRow = new QHBoxLayout;
    dirRow->addWidget(m_dirEdit, 1);
    auto* browseBtn = new QPushButton(QStringLiteral("浏览…"), this);
    dirRow->addWidget(browseBtn);
    form->addRow(QStringLiteral("导出到目录"), dirRow);

    m_nameEdit = new QLineEdit(defaultBaseName, this);
    form->addRow(QStringLiteral("文件名前缀"), m_nameEdit);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browseBtn, &QPushButton::clicked, this, &ExportDialog::browse);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(buttons);
}

void ExportDialog::browse()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择导出目录"), m_dirEdit->text());
    if (!dir.isEmpty())
        m_dirEdit->setText(QDir::toNativeSeparators(dir));
}

AssetExporter::Format ExportDialog::format() const
{
    return AssetExporter::formatFromKey(m_formatCombo->currentData().toString());
}

QString ExportDialog::targetDir() const
{
    return QDir::fromNativeSeparators(m_dirEdit->text().trimmed());
}

QString ExportDialog::baseName() const
{
    QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty())
        name = QStringLiteral("asset");
    // 去掉不适合做文件名的字符
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    return name;
}

} // namespace gc
