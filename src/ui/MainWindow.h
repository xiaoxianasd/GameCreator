#pragma once

// MainWindow：创作工作台主窗口。
// 左侧：竖向窄导航栏（类似竖过来的任务栏），切换功能页面
// 右侧：毛玻璃圆角分页（生成 / 候选与处理 / 日志 / 设置）
// 背景：WallpaperBackground 在线动漫壁纸 / 本地图片定时轮换

#include "core/AssetSpec.h"
#include "core/ReviewReport.h"
#include "export/AssetExporter.h"
#include "models/ModelRouter.h"
#include "orchestrator/TaskOrchestrator.h"
#include "storage/ProjectStore.h"

#include <QMainWindow>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTextEdit;
class QToolButton;
class QWidget;

namespace gc {

class WallpaperBackground;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onGenerate();
    void onCancel();
    void onNewAsset();
    void onPickReference();
    void onTypeChanged(int index);
    void onSetMaster();
    void onLockFrame();
    void onExport();
    void onShowReport();
    void onSettings();
    void onNavSwitch(int index);
    void onBrowseBgDir();
    void onApplyBgSettings();

private:
    struct CandInfo {
        QString versionId;
        QString direction;   // "down"/... 或空
        QString rawAbsPath;
        qlonglong seed = 0;
        int seqInDir = 0;
    };

    void buildUi();
    QWidget* buildGeneratePage();
    QWidget* buildCandidatesPage();
    QWidget* buildLogPage();
    QWidget* buildSettingsPage();
    void applyAppStyle();
    void switchPage(int index);

    void appendLog(const QString& line);
    AssetSpec buildSpecFromUi() const;
    QVector<Direction> checkedDirections() const;
    CandInfo currentCandidate(int* rowOut = nullptr) const;
    AssetRecord currentRecord() const;
    void rebuildAssetStatus();
    void setUiBusy(bool busy);

    // 标准化选中候选 → 保存 processed 版本；返回相对路径（失败返回空）
    QString standardizeSelected(AssetSpec* specOut = nullptr, QImage* frameOut = nullptr);

    // 弹出导出对话框（格式 + 目录），执行导出并写日志；取消返回空
    QStringList exportToUserDir(const AssetExporter::SheetData& data, const QString& defaultBaseName);

    ProjectStore* m_store = nullptr;
    ProjectStore::StylePack m_stylePack;
    ModelRouter::Config m_cfg;
    TaskOrchestrator* m_orch = nullptr;

    QString m_currentAssetId;
    QVector<CandInfo> m_candidates;
    ReviewReport m_lastReport;
    QString m_referencePath;

    // 框架：背景 + 导航 + 分页
    WallpaperBackground* m_bg = nullptr;
    QStackedWidget* m_stack = nullptr;
    QVector<QToolButton*> m_navButtons;

    // 生成页表单
    QComboBox* m_typeCombo = nullptr;
    QTextEdit* m_promptEdit = nullptr;
    QLineEdit* m_negativeEdit = nullptr;
    QComboBox* m_frameSizeCombo = nullptr;
    QSpinBox* m_candidateSpin = nullptr;
    QComboBox* m_modeCombo = nullptr;
    QLabel* m_referenceLabel = nullptr;
    QPushButton* m_referenceBtn = nullptr;
    QCheckBox* m_dirChecks[4] = { nullptr, nullptr, nullptr, nullptr }; // 下上左右
    QPushButton* m_generateBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_newAssetBtn = nullptr;
    QPushButton* m_settingsBtn = nullptr;

    // 候选页
    QListWidget* m_candidateList = nullptr;
    QPushButton* m_masterBtn = nullptr;
    QPushButton* m_lockBtn = nullptr;
    QPushButton* m_exportBtn = nullptr;
    QPushButton* m_reportBtn = nullptr;
    QLabel* m_assetStatusLabel = nullptr;

    // 日志页
    QPlainTextEdit* m_logEdit = nullptr;

    // 设置页（背景）
    QComboBox* m_bgSourceCombo = nullptr;
    QLineEdit* m_bgDirEdit = nullptr;
    QSpinBox* m_bgIntervalSpin = nullptr;
};

} // namespace gc
