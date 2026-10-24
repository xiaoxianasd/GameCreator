#pragma once

// ExportDialog：导出选项对话框（选择导出格式与目标目录）。

#include "export/AssetExporter.h"

#include <QDialog>

class QComboBox;
class QLineEdit;

namespace gc {

class ExportDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExportDialog(QWidget* parent, const QString& defaultBaseName);

    AssetExporter::Format format() const;
    QString targetDir() const;
    QString baseName() const;

private:
    void browse();

    QComboBox* m_formatCombo = nullptr;
    QLineEdit* m_dirEdit = nullptr;
    QLineEdit* m_nameEdit = nullptr;
};

} // namespace gc
