#include "ui/MainWindow.h"

#include "checks/DeterministicChecks.h"
#include "checks/VisualHeuristics.h"
#include "factory/AssetFactory.h"
#include "ui/ExportDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/GlassPanel.h"
#include "ui/WallpaperBackground.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace gc {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // ---- 配置与存储 ----
    const QString cfgPath = ModelRouter::defaultConfigPath();
    m_cfg = ModelRouter::load(cfgPath, true);

    // 项目默认落在仓库根目录的 projects/；首次运行把旧的 build/projects 迁移过来
    const QString legacyRoot = QCoreApplication::applicationDirPath() + QStringLiteral("/projects");
    const QString projectsRoot = ProjectStore::resolveProjectsRoot();
    QString migrationNote;
    if (QDir::cleanPath(legacyRoot) != projectsRoot)
        ProjectStore::migrateLegacyProjects(legacyRoot, projectsRoot, &migrationNote);
    m_store = new ProjectStore(projectsRoot, QStringLiteral("default"));
    QString err;
    if (!m_store->createOrOpen(&err))
        QMessageBox::warning(this, QStringLiteral("项目"), err);
    m_stylePack = m_store->loadStylePack();

    m_orch = new TaskOrchestrator(m_store, m_cfg, this);
    connect(m_orch, &TaskOrchestrator::logMessage, this, &MainWindow::appendLog);
    connect(m_orch, &TaskOrchestrator::candidateReady, this,
            [this](const QString& assetId, const QString& versionId, const QString& direction,
                   const QString& rawPath, qlonglong seed, const QString& model) {
                Q_UNUSED(assetId)
                CandInfo c;
                c.versionId = versionId;
                c.direction = direction;
                c.rawAbsPath = m_store->absolutePath(rawPath);
                c.seed = seed;
                c.seqInDir = int(m_candidates.size()) + 1;
                m_candidates.append(c);

                QPixmap pm(c.rawAbsPath);
                if (!pm.isNull())
                    pm = pm.scaled(112, 112, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                QListWidgetItem* item = new QListWidgetItem(
                    QIcon(pm),
                    QStringLiteral("%1 #%2")
                        .arg(c.direction.isEmpty() ? QStringLiteral("候选")
                                                   : directionToDisplay(directionFromString(c.direction)))
                        .arg(c.seqInDir));
                item->setToolTip(QStringLiteral("version: %1\nseed: %2\nmodel: %3")
                                     .arg(versionId).arg(seed).arg(model));
                m_candidateList->addItem(item);
            });
    connect(m_orch, &TaskOrchestrator::jobFinished, this,
            [this](const QString& assetId, int ok, int fail) {
                Q_UNUSED(assetId)
                Q_UNUSED(fail)
                setUiBusy(false);
                if (ok == 0)
                    m_assetStatusLabel->setText(QStringLiteral("生成失败，请检查日志与配置。"));
                else
                    rebuildAssetStatus();
            });

    buildUi();

    appendLog(QStringLiteral("项目目录：%1").arg(m_store->projectDir()));
    if (!migrationNote.isEmpty())
        appendLog(migrationNote);
    appendLog(QStringLiteral("配置文件：%1").arg(cfgPath));
    appendLog(QStringLiteral("当前适配器模式：%1（可在「生成」页切换）")
                  .arg(ModelRouter::modeToDisplay(ModelRouter::resolveMode(m_cfg))));
    appendLog(QStringLiteral("默认离线模拟模式可完整验证流程；接入真实模型请编辑 config.json。"));
    rebuildAssetStatus();
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("GameCreator — AI 像素游戏素材工作台"));
    resize(1240, 800);
    setMinimumSize(1024, 640);

    // ---- 背景层：在线动漫壁纸 / 本地图片轮换 ----
    m_bg = new WallpaperBackground(this);
    QSettings settings;
    m_bg->setSourceMode(settings.value(QStringLiteral("bg/source"),
                                       int(WallpaperBackground::OnlineFirst)).toInt());
    m_bg->setLocalDir(settings.value(QStringLiteral("bg/dir")).toString());
    m_bg->setIntervalSeconds(settings.value(QStringLiteral("bg/interval"), 30).toInt());
    connect(m_bg, &WallpaperBackground::statusMessage, this, &MainWindow::appendLog);

    auto* root = new QHBoxLayout(m_bg);
    root->setContentsMargins(26, 22, 26, 22);
    root->setSpacing(22);

    // ---- 左侧竖向导航栏（类任务栏）----
    auto* nav = new GlassPanel(m_bg);
    nav->setTint(QColor(22, 26, 36, 165)); // 深色磨砂，衬白色文字
    nav->setCornerRadius(18);
    nav->setFixedWidth(80);
    auto* navLay = new QVBoxLayout(nav);
    navLay->setContentsMargins(8, 16, 8, 14);
    navLay->setSpacing(10);

    auto* logo = new QLabel(QStringLiteral("GC"), nav);
    logo->setAlignment(Qt::AlignCenter);
    logo->setStyleSheet(QStringLiteral(
        "color:#fff; font-size:17px; font-weight:bold;"
        "background:rgba(255,255,255,36); border-radius:14px; padding:9px 0;"));
    navLay->addWidget(logo);
    navLay->addSpacing(6);

    const QString navNames[4] = { QStringLiteral("✎\n生成"), QStringLiteral("❖\n候选"),
                                  QStringLiteral("☰\n日志"), QStringLiteral("⚙\n设置") };
    for (int i = 0; i < 4; ++i) {
        auto* btn = new QToolButton(nav);
        btn->setObjectName(QStringLiteral("navBtn"));
        btn->setText(navNames[i]);
        btn->setCheckable(true);
        btn->setAutoExclusive(true);
        btn->setFixedSize(64, 56);
        connect(btn, &QToolButton::clicked, this, [this, i] { onNavSwitch(i); });
        navLay->addWidget(btn, 0, Qt::AlignHCenter);
        m_navButtons.append(btn);
    }
    navLay->addStretch(1);

    // ---- 右侧分页 ----
    m_stack = new QStackedWidget(m_bg);
    m_stack->addWidget(buildGeneratePage());
    m_stack->addWidget(buildCandidatesPage());
    m_stack->addWidget(buildLogPage());
    m_stack->addWidget(buildSettingsPage());

    root->addWidget(nav);
    root->addWidget(m_stack, 1);

    setCentralWidget(m_bg);
    applyAppStyle();
    switchPage(0);

    // 调试：GC_PAGE=n 指定初始页面（配合 GC_SCREENSHOT 走查各页）
    bool ok = false;
    const int dbgPage = qEnvironmentVariableIntValue("GC_PAGE", &ok);
    if (ok)
        switchPage(dbgPage);
}

void MainWindow::switchPage(int index)
{
    if (!m_stack)
        return;
    m_stack->setCurrentIndex(index);
    for (int i = 0; i < m_navButtons.size(); ++i)
        m_navButtons[i]->setChecked(i == index);
}

void MainWindow::onNavSwitch(int index)
{
    switchPage(index);
}

QWidget* MainWindow::buildGeneratePage()
{
    // 透明容器 + 居中收缩的玻璃面板，让四周与下方露出背景
    auto* container = new QWidget(m_stack);
    auto* outer = new QVBoxLayout(container);
    outer->setContentsMargins(40, 34, 40, 34);

    auto* page = new GlassPanel(container);
    page->setMinimumWidth(620);
    page->setMaximumWidth(720);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(24, 20, 24, 20);
    lay->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("素材生成"), page);
    title->setObjectName(QStringLiteral("pageTitle"));
    lay->addWidget(title);

    // 表单限制最大宽度，避免在宽窗口下被拉得过长
    auto* wrap = new QWidget(page);
    wrap->setMaximumWidth(640);
    auto* wrapLay = new QVBoxLayout(wrap);
    wrapLay->setContentsMargins(0, 0, 0, 0);
    wrapLay->setSpacing(10);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    m_typeCombo = new QComboBox(wrap);
    m_typeCombo->addItem(QStringLiteral("角色"));       // 0
    m_typeCombo->addItem(QStringLiteral("物品"));       // 1
    m_typeCombo->addItem(QStringLiteral("场景概念图")); // 2
    form->addRow(QStringLiteral("素材类型"), m_typeCombo);

    m_promptEdit = new QTextEdit(wrap);
    m_promptEdit->setPlaceholderText(
        QStringLiteral("例：穿蓝色斗篷的少年剑士，银色短发…（物品可写：铁剑 / 治疗药水 / 宝箱）"));
    m_promptEdit->setFixedHeight(72);
    form->addRow(QStringLiteral("提示词"), m_promptEdit);

    m_negativeEdit = new QLineEdit(wrap);
    m_negativeEdit->setPlaceholderText(QStringLiteral("例：文字、水印、模糊（可留空）"));
    form->addRow(QStringLiteral("排除内容"), m_negativeEdit);

    m_frameSizeCombo = new QComboBox(wrap);
    for (int s : { 32, 48, 64, 128 })
        m_frameSizeCombo->addItem(QStringLiteral("%1 x %1 像素").arg(s), s);
    m_frameSizeCombo->setCurrentIndex(2); // 默认 64
    form->addRow(QStringLiteral("目标帧尺寸"), m_frameSizeCombo);

    m_candidateSpin = new QSpinBox(wrap);
    m_candidateSpin->setRange(1, 8);
    m_candidateSpin->setValue(m_cfg.candidates);
    form->addRow(QStringLiteral("每方向候选数"), m_candidateSpin);

    m_modeCombo = new QComboBox(wrap);
    m_modeCombo->addItem(QStringLiteral("自动路由"), QStringLiteral("auto"));
    m_modeCombo->addItem(QStringLiteral("云端 API"), QStringLiteral("cloud"));
    m_modeCombo->addItem(QStringLiteral("本地 ComfyUI"), QStringLiteral("comfyui"));
    m_modeCombo->addItem(QStringLiteral("离线模拟（无需 API）"), QStringLiteral("mock"));
    form->addRow(QStringLiteral("生成模式"), m_modeCombo);

    // 参考图
    auto* refRow = new QHBoxLayout();
    m_referenceBtn = new QPushButton(QStringLiteral("选择参考图…"), wrap);
    auto* refClear = new QPushButton(QStringLiteral("清除"), wrap);
    refRow->addWidget(m_referenceBtn);
    refRow->addWidget(refClear);
    form->addRow(QStringLiteral("参考图"), refRow);
    m_referenceLabel = new QLabel(QStringLiteral("（无）"), wrap);
    m_referenceLabel->setWordWrap(true);
    form->addRow(QString(), m_referenceLabel);
    connect(m_referenceBtn, &QPushButton::clicked, this, &MainWindow::onPickReference);
    connect(refClear, &QPushButton::clicked, this, [this] {
        m_referencePath.clear();
        m_referenceLabel->setText(QStringLiteral("（无）"));
    });

    wrapLay->addLayout(form);

    // 方向选择（角色）
    auto* dirBox = new QGroupBox(QStringLiteral("生成方向（角色）"), wrap);
    auto* dirLay = new QHBoxLayout(dirBox);
    const QString dirNames[4] = { QStringLiteral("下(正面)"), QStringLiteral("上(背面)"),
                                  QStringLiteral("左"), QStringLiteral("右") };
    const Direction dirVals[4] = { Direction::Down, Direction::Up, Direction::Left, Direction::Right };
    for (int i = 0; i < 4; ++i) {
        m_dirChecks[i] = new QCheckBox(dirNames[i], dirBox);
        if (i == 0)
            m_dirChecks[i]->setChecked(true);
        dirLay->addWidget(m_dirChecks[i]);
    }
    wrapLay->addWidget(dirBox);

    // 操作按钮
    m_generateBtn = new QPushButton(QStringLiteral("开始生成"), wrap);
    m_generateBtn->setObjectName(QStringLiteral("primaryBtn"));
    m_generateBtn->setMinimumHeight(38);
    QFont f = m_generateBtn->font();
    f.setBold(true);
    m_generateBtn->setFont(f);
    m_cancelBtn = new QPushButton(QStringLiteral("取消任务"), wrap);
    m_cancelBtn->setEnabled(false);
    auto* genRow = new QHBoxLayout();
    genRow->addWidget(m_generateBtn);
    genRow->addWidget(m_cancelBtn);
    wrapLay->addLayout(genRow);

    m_newAssetBtn = new QPushButton(QStringLiteral("新建素材（重置身份与锁定）"), wrap);
    wrapLay->addWidget(m_newAssetBtn);

    m_settingsBtn = new QPushButton(QStringLiteral("模型与 API 设置…"), wrap);
    wrapLay->addWidget(m_settingsBtn);

    lay->addWidget(wrap, 0, Qt::AlignHCenter);
    lay->addStretch(1);

    connect(m_generateBtn, &QPushButton::clicked, this, &MainWindow::onGenerate);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MainWindow::onCancel);
    connect(m_newAssetBtn, &QPushButton::clicked, this, &MainWindow::onNewAsset);
    connect(m_settingsBtn, &QPushButton::clicked, this, &MainWindow::onSettings);
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onTypeChanged);

    outer->addWidget(page, 0, Qt::AlignLeft);
    outer->addStretch(1);
    return container;
}

QWidget* MainWindow::buildCandidatesPage()
{
    // 候选网格需要足够高度，面板填充内容区但四周留边露出背景
    auto* container = new QWidget(m_stack);
    auto* outer = new QVBoxLayout(container);
    outer->setContentsMargins(32, 28, 32, 28);

    auto* page = new GlassPanel(container);
    page->setMinimumWidth(620);
    page->setMaximumWidth(840);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(24, 20, 24, 20);
    lay->setSpacing(8);

    auto* headRow = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("候选与处理"), page);
    title->setObjectName(QStringLiteral("pageTitle"));
    m_assetStatusLabel = new QLabel(page);
    m_assetStatusLabel->setWordWrap(true);
    m_assetStatusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_assetStatusLabel->setStyleSheet(QStringLiteral("color:#4a4f58; background:transparent;"));
    headRow->addWidget(title);
    headRow->addStretch(1);
    headRow->addWidget(m_assetStatusLabel);
    lay->addLayout(headRow);

    lay->addWidget(new QLabel(QStringLiteral("候选图（点击选择）"), page));
    m_candidateList = new QListWidget(page);
    m_candidateList->setViewMode(QListView::IconMode);
    m_candidateList->setIconSize(QSize(112, 112));
    m_candidateList->setResizeMode(QListView::Adjust);
    m_candidateList->setMovement(QListView::Static);
    m_candidateList->setSpacing(8);
    m_candidateList->setSelectionMode(QAbstractItemView::SingleSelection);
    lay->addWidget(m_candidateList, 1);

    auto* btnRow = new QHBoxLayout();
    m_masterBtn = new QPushButton(QStringLiteral("① 设为母版"), page);
    m_lockBtn = new QPushButton(QStringLiteral("② 标准化并锁定"), page);
    m_exportBtn = new QPushButton(QStringLiteral("③ 导出素材包"), page);
    m_reportBtn = new QPushButton(QStringLiteral("查看检查报告"), page);
    btnRow->addWidget(m_masterBtn);
    btnRow->addWidget(m_lockBtn);
    btnRow->addWidget(m_exportBtn);
    btnRow->addWidget(m_reportBtn);
    btnRow->addStretch(1);
    lay->addLayout(btnRow);

    connect(m_masterBtn, &QPushButton::clicked, this, &MainWindow::onSetMaster);
    connect(m_lockBtn, &QPushButton::clicked, this, &MainWindow::onLockFrame);
    connect(m_exportBtn, &QPushButton::clicked, this, &MainWindow::onExport);
    connect(m_reportBtn, &QPushButton::clicked, this, &MainWindow::onShowReport);

    // 垂直填满、水平靠左限宽，右侧露出壁纸
    auto* hRow = new QHBoxLayout();
    hRow->setContentsMargins(0, 0, 0, 0);
    hRow->addWidget(page, 1);
    hRow->addStretch(1);
    outer->addLayout(hRow, 1);
    return container;
}

QWidget* MainWindow::buildLogPage()
{
    auto* container = new QWidget(m_stack);
    auto* outer = new QVBoxLayout(container);
    outer->setContentsMargins(32, 28, 32, 28);

    auto* page = new GlassPanel(container);
    page->setMinimumWidth(620);
    page->setMaximumWidth(840);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(24, 20, 24, 20);
    lay->setSpacing(8);

    auto* title = new QLabel(QStringLiteral("运行日志"), page);
    title->setObjectName(QStringLiteral("pageTitle"));
    lay->addWidget(title);

    m_logEdit = new QPlainTextEdit(page);
    m_logEdit->setReadOnly(true);
    m_logEdit->setPlaceholderText(QStringLiteral("运行日志"));
    lay->addWidget(m_logEdit, 1);

    auto* hRow = new QHBoxLayout();
    hRow->setContentsMargins(0, 0, 0, 0);
    hRow->addWidget(page, 1);
    hRow->addStretch(1);
    outer->addLayout(hRow, 1);
    return container;
}

QWidget* MainWindow::buildSettingsPage()
{
    auto* container = new QWidget(m_stack);
    auto* outer = new QVBoxLayout(container);
    outer->setContentsMargins(40, 34, 40, 34);

    auto* page = new GlassPanel(container);
    page->setMinimumWidth(620);
    page->setMaximumWidth(720);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(24, 20, 24, 20);
    lay->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("设置"), page);
    title->setObjectName(QStringLiteral("pageTitle"));
    lay->addWidget(title);

    auto* wrap = new QWidget(page);
    wrap->setMaximumWidth(640);
    auto* form = new QFormLayout(wrap);
    form->setLabelAlignment(Qt::AlignRight);

    m_bgSourceCombo = new QComboBox(wrap);    m_bgSourceCombo->addItem(QStringLiteral("在线动漫壁纸"), WallpaperBackground::Online);
    m_bgSourceCombo->addItem(QStringLiteral("本地图片文件夹"), WallpaperBackground::Local);
    m_bgSourceCombo->addItem(QStringLiteral("在线优先，失败时用本地图片"), WallpaperBackground::OnlineFirst);
    m_bgSourceCombo->setCurrentIndex(m_bg->sourceMode());
    form->addRow(QStringLiteral("背景来源"), m_bgSourceCombo);

    auto* dirRow = new QHBoxLayout();
    m_bgDirEdit = new QLineEdit(wrap);
    m_bgDirEdit->setPlaceholderText(
        QStringLiteral("留空则使用默认目录：程序目录/wallpapers"));
    m_bgDirEdit->setText(m_bg->localDir());
    auto* browseBtn = new QPushButton(QStringLiteral("浏览…"), wrap);
    connect(browseBtn, &QPushButton::clicked, this, &MainWindow::onBrowseBgDir);
    dirRow->addWidget(m_bgDirEdit, 1);
    dirRow->addWidget(browseBtn);
    form->addRow(QStringLiteral("本地图片文件夹"), dirRow);

    m_bgIntervalSpin = new QSpinBox(wrap);
    m_bgIntervalSpin->setRange(5, 600);
    m_bgIntervalSpin->setSuffix(QStringLiteral(" 秒"));
    m_bgIntervalSpin->setValue(m_bg->intervalSeconds());
    form->addRow(QStringLiteral("轮换间隔"), m_bgIntervalSpin);

    lay->addWidget(wrap, 0, Qt::AlignHCenter);

    auto* applyBtn = new QPushButton(QStringLiteral("应用并重新加载背景"), page);
    applyBtn->setObjectName(QStringLiteral("primaryBtn"));
    applyBtn->setFixedWidth(220);
    connect(applyBtn, &QPushButton::clicked, this, &MainWindow::onApplyBgSettings);
    lay->addWidget(applyBtn, 0, Qt::AlignHCenter);

    auto* hint = new QLabel(QStringLiteral(
        "在线模式从公开动漫图床随机获取横版壁纸；网络不可用时会自动回退到本地文件夹。\n"
        "也可以把自己喜欢的图片放进本地文件夹，选择「本地图片文件夹」模式即可轮换。"), page);
    hint->setWordWrap(true);
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet(QStringLiteral("color:#4a4f58; background:transparent;"));
    lay->addWidget(hint);
    lay->addStretch(1);

    outer->addWidget(page, 0, Qt::AlignLeft);
    outer->addStretch(1);
    return container;
}

void MainWindow::onBrowseBgDir()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择本地壁纸文件夹"), m_bgDirEdit->text());
    if (!dir.isEmpty())
        m_bgDirEdit->setText(dir);
}

void MainWindow::onApplyBgSettings()
{
    m_bg->setSourceMode(m_bgSourceCombo->currentData().toInt());
    m_bg->setLocalDir(m_bgDirEdit->text().trimmed());
    m_bg->setIntervalSeconds(m_bgIntervalSpin->value());

    QSettings settings;
    settings.setValue(QStringLiteral("bg/source"), m_bg->sourceMode());
    settings.setValue(QStringLiteral("bg/dir"), m_bg->localDir());
    settings.setValue(QStringLiteral("bg/interval"), m_bg->intervalSeconds());

    m_bg->applyAndReload();
    appendLog(QStringLiteral("背景设置已应用：来源=%1，间隔=%2 秒")
                  .arg(m_bgSourceCombo->currentText())
                  .arg(m_bg->intervalSeconds()));
}

void MainWindow::applyAppStyle()
{
    setStyleSheet(QStringLiteral(R"(
QLabel { background: transparent; color: #2b2f36; }
QLabel#pageTitle { font-size: 18px; font-weight: bold; color: #2b2f36; }
QLineEdit, QComboBox, QSpinBox, QTextEdit, QPlainTextEdit, QListWidget {
    background: rgba(255, 255, 255, 150);
    border: 1px solid rgba(255, 255, 255, 190);
    border-radius: 8px;
    padding: 5px 8px;
    selection-background-color: #86a8ff;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus,
QTextEdit:focus, QPlainTextEdit:focus, QListWidget:focus {
    border: 1px solid #7aa0f0;
}
QComboBox QAbstractItemView {
    background: rgba(252, 253, 255, 245);
    border: 1px solid rgba(180, 190, 210, 200);
    selection-background-color: #dfe9ff;
}
QPushButton {
    background: rgba(255, 255, 255, 165);
    border: 1px solid rgba(255, 255, 255, 200);
    border-radius: 9px;
    padding: 7px 14px;
    color: #2b2f36;
}
QPushButton:hover { background: rgba(255, 255, 255, 225); }
QPushButton:pressed { background: rgba(222, 232, 252, 235); }
QPushButton:disabled { color: rgba(90, 95, 105, 140); background: rgba(255, 255, 255, 80); }
QPushButton#primaryBtn {
    background: rgba(74, 124, 240, 220);
    border: 1px solid rgba(74, 124, 240, 240);
    color: white;
    font-weight: bold;
}
QPushButton#primaryBtn:hover { background: rgba(88, 136, 248, 235); }
QPushButton#primaryBtn:disabled { background: rgba(120, 150, 220, 120); color: rgba(255, 255, 255, 180); }
QGroupBox {
    background: rgba(255, 255, 255, 70);
    border: 1px solid rgba(255, 255, 255, 150);
    border-radius: 10px;
    margin-top: 14px;
    padding-top: 10px;
    color: #2b2f36;
}
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; }
QCheckBox { background: transparent; color: #2b2f36; spacing: 6px; }
QToolButton#navBtn {
    color: rgba(255, 255, 255, 210);
    background: transparent;
    border: none;
    border-radius: 12px;
    font-size: 13px;
}
QToolButton#navBtn:hover { background: rgba(255, 255, 255, 38); }
QToolButton#navBtn:checked { background: rgba(255, 255, 255, 64); color: #ffffff; }
QListWidget::item { border-radius: 8px; }
QListWidget::item:selected { background: rgba(122, 160, 240, 90); border: 1px solid #7aa0f0; }
)"));
}

// ---------------------------------------------------------------- actions

QVector<Direction> MainWindow::checkedDirections() const
{
    const Direction vals[4] = { Direction::Down, Direction::Up, Direction::Left, Direction::Right };
    QVector<Direction> out;
    for (int i = 0; i < 4; ++i)
        if (m_dirChecks[i]->isChecked())
            out.append(vals[i]);
    return out;
}

AssetSpec MainWindow::buildSpecFromUi() const
{
    AssetSpec spec;
    spec.id = newId(QStringLiteral("spec"));
    spec.type = AssetType(m_typeCombo->currentIndex());
    spec.prompt = m_promptEdit->toPlainText().trimmed();
    spec.negativePrompt = m_negativeEdit->text().trimmed();
    spec.directions = spec.type == AssetType::Character
                          ? checkedDirections()
                          : QVector<Direction>{ Direction::Down };
    spec.frameSize = m_frameSizeCombo->currentData().toInt();
    spec.candidateCount = m_candidateSpin->value();
    spec.paletteSize = m_stylePack.paletteSize;
    // StylePack 参与生成：光照与透视进入提示词策略
    spec.styleLight = m_stylePack.lightDirection;
    spec.stylePerspective = m_stylePack.perspective;
    spec.modelWidth = 512;
    spec.modelHeight = 512;
    if (!m_referencePath.isEmpty())
        spec.referenceImages.append(m_referencePath);
    return spec;
}

void MainWindow::onTypeChanged(int index)
{
    const bool isChar = index == 0;
    const bool isScene = index == 2;
    for (int i = 0; i < 4; ++i)
        m_dirChecks[i]->setEnabled(isChar);
    m_masterBtn->setEnabled(isChar);
    m_lockBtn->setText(isChar ? QStringLiteral("② 标准化并锁定该方向")
                              : QStringLiteral("② 标准化该候选"));
    m_exportBtn->setText(isChar ? QStringLiteral("③ 导出 Sprite Sheet")
                                : QStringLiteral("③ 导出素材包"));
    // 场景概念图会按 2 倍尺寸输出，这里明确提示，避免“选了 64 却得到 128”的困惑
    m_frameSizeCombo->setToolTip(
        isScene ? QStringLiteral("场景概念图按 2× 输出（64 → 128×128），保留更多画面细节")
                : QStringLiteral("单帧输出的像素尺寸"));
}

void MainWindow::onPickReference()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择参考图"), QString(),
        QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp *.webp);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    m_referencePath = path;
    m_referenceLabel->setText(QDir::toNativeSeparators(path));
}

void MainWindow::onGenerate()
{
    AssetSpec spec = buildSpecFromUi();
    if (spec.prompt.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先输入提示词。"));
        return;
    }
    if (spec.type == AssetType::Character && spec.directions.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("角色素材请至少选择一个方向。"));
        return;
    }
    if (spec.type == AssetType::Scene) {
        appendLog(QStringLiteral("场景概念图按 2× 输出：%1×%1（目标帧尺寸 %2）")
                      .arg(spec.outputSize())
                      .arg(spec.frameSize));
    }

    // 生成模式覆盖
    const QString mode = m_modeCombo->currentData().toString();
    if (mode != m_cfg.mode) {
        m_cfg.mode = mode;
        appendLog(QStringLiteral("生成模式已切换为：%1").arg(ModelRouter::modeToDisplay(mode)));
    }

    // 复用当前素材；类型变化或首次生成时新建
    AssetRecord rec = currentRecord();
    if (m_currentAssetId.isEmpty() || rec.type != spec.type || rec.id.isEmpty()) {
        rec = m_store->createAsset(spec.prompt.left(12), spec.type);
        m_currentAssetId = rec.id;
        m_candidates.clear();
        m_candidateList->clear();
        appendLog(QStringLiteral("新建素材：%1（%2）").arg(rec.name, rec.id));
    }

    // 身份一致性：角色且当前素材已有母版 → 自动引用母版（技术路线 8.1）
    if (spec.type == AssetType::Character && spec.referenceImages.isEmpty()
        && !rec.masterVersionId.isEmpty()) {
        const QString masterPath = rec.masterFilePath();
        if (!masterPath.isEmpty()) {
            spec.referenceImages.append(m_store->absolutePath(masterPath));
            appendLog(QStringLiteral("已自动引用已确认母版作为参考图（保持身份一致）。"));
        }
    }

    spec.assetId = m_currentAssetId;
    m_store->saveSpec(m_currentAssetId, spec);

    m_lastReport = ReviewReport();
    setUiBusy(true);
    m_assetStatusLabel->setText(QStringLiteral("生成中…"));
    m_orch->submit(spec);
    switchPage(1); // 提交后跳到候选页等待结果
}

void MainWindow::onCancel()
{
    m_orch->cancel();
    setUiBusy(false);
}

void MainWindow::onSettings()
{
    if (m_orch && m_orch->busy()) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("任务正在进行，请等待完成后再修改设置。"));
        return;
    }
    SettingsDialog dlg(this, m_cfg);
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_cfg = dlg.config();
    const QString cfgPath = ModelRouter::defaultConfigPath();
    if (!ModelRouter::save(cfgPath, m_cfg)) {
        QMessageBox::warning(this, QStringLiteral("错误"),
                             QStringLiteral("配置写入失败：%1").arg(cfgPath));
        return;
    }
    if (m_orch)
        m_orch->setConfig(m_cfg);
    appendLog(QStringLiteral("设置已保存到 %1，当前生效模式：%2")
                  .arg(QDir::toNativeSeparators(cfgPath),
                       ModelRouter::modeToDisplay(ModelRouter::resolveMode(m_cfg))));
    if (!m_cfg.cloud.apiKey.isEmpty() && m_cfg.cloud.baseUrl.isEmpty())
        appendLog(QStringLiteral("提示：已填密钥但 base_url 为空，云端模式不可用。"));
}

void MainWindow::onNewAsset()
{
    m_currentAssetId.clear();
    m_candidates.clear();
    m_candidateList->clear();
    m_lastReport = ReviewReport();
    appendLog(QStringLiteral("已重置：下一次生成将创建新素材身份。"));
    rebuildAssetStatus();
}

MainWindow::CandInfo MainWindow::currentCandidate(int* rowOut) const
{
    const int row = m_candidateList->currentRow();
    if (rowOut)
        *rowOut = row;
    if (row < 0 || row >= m_candidates.size())
        return CandInfo();
    return m_candidates[row];
}

AssetRecord MainWindow::currentRecord() const
{
    if (m_currentAssetId.isEmpty())
        return AssetRecord();
    return m_store->loadAssetRecord(m_currentAssetId);
}

void MainWindow::onSetMaster()
{
    const CandInfo c = currentCandidate();
    if (c.versionId.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先在右侧选择一张候选图。"));
        return;
    }
    AssetRecord rec = currentRecord();
    rec.masterVersionId = c.versionId;
    m_store->saveAssetRecord(rec);
    appendLog(QStringLiteral("母版已设定：%1（%2）")
                  .arg(c.versionId,
                       c.direction.isEmpty() ? QStringLiteral("单帧")
                                             : directionToDisplay(directionFromString(c.direction))));
    rebuildAssetStatus();
}

QString MainWindow::standardizeSelected(AssetSpec* specOut, QImage* frameOut)
{
    const CandInfo c = currentCandidate();
    if (c.versionId.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选择一张候选图。"));
        return QString();
    }
    AssetRecord rec = currentRecord();
    AssetSpec spec = rec.lastSpec;
    if (spec.assetId.isEmpty())
        spec = m_store->loadSpec(m_currentAssetId);
    if (spec.assetId.isEmpty()) {
        spec = buildSpecFromUi();
        spec.assetId = m_currentAssetId;
    }
    if (spec.frameSize <= 0) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("缺少素材规格。"));
        return QString();
    }

    QImage raw(c.rawAbsPath);
    if (raw.isNull()) {
        QMessageBox::warning(this, QStringLiteral("错误"),
                             QStringLiteral("无法读取原始图片：%1").arg(c.rawAbsPath));
        return QString();
    }

    // 统一色板（角色）：已有母版 → 从母版原始图构建一次，所有方向共用，防止色彩漂移
    QVector<QRgb> sharedPalette;
    if (spec.type == AssetType::Character) {
        if (!rec.palette.isEmpty()) {
            sharedPalette = rec.palette;
        } else if (!rec.masterVersionId.isEmpty()) {
            const QString masterPath = m_store->absolutePath(rec.masterFilePath());
            QImage masterRaw(masterPath);
            if (!masterRaw.isNull()) {
                rec.palette = AssetFactory::paletteFromRaw(masterRaw, spec);
                sharedPalette = rec.palette;
                appendLog(QStringLiteral("已从母版提取统一色板（%1 色），后续方向共用。")
                              .arg(rec.palette.size()));
            }
        }
    }

    StandardizeResult result = AssetFactory::standardize(raw, spec, sharedPalette);
    if (result.frame.isNull()) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("标准化处理失败。"));
        return QString();
    }

    const QString versionId = newId(QStringLiteral("v"));
    const QString rel = m_store->saveProcessedImage(m_currentAssetId, versionId,
                                                    result.frame, c.direction);
    if (rel.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("处理结果保存失败。"));
        return QString();
    }

    // 版本记录
    AssetVersion v;
    v.versionId = versionId;
    v.filePath = rel;
    v.kind = QStringLiteral("processed");
    v.direction = c.direction;
    v.model = QStringLiteral("asset-factory");
    v.seed = c.seed;
    v.created = QDateTime::currentDateTime();
    rec.versions.append(v);

    // 锁定：角色 → 对应方向；物品/场景 → single
    const QString lockKey = c.direction.isEmpty() ? QStringLiteral("single") : c.direction;
    rec.lockedFrames.insert(lockKey, versionId);
    m_store->saveAssetRecord(rec);

    appendLog(QStringLiteral("已标准化并锁定：%1 %2 → %3（%4x%4，%5 色）")
                  .arg(lockKey, c.versionId, versionId)
                  .arg(spec.frameSize)
                  .arg(result.palette.size()));
    if (specOut)
        *specOut = spec;
    if (frameOut)
        *frameOut = result.frame;
    rebuildAssetStatus();
    return m_store->absolutePath(rel);
}

void MainWindow::onLockFrame()
{
    standardizeSelected();
}

void MainWindow::onExport()
{
    AssetRecord rec = currentRecord();
    if (rec.id.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("当前没有素材。"));
        return;
    }
    AssetSpec spec = rec.lastSpec;
    if (spec.assetId.isEmpty())
        spec = m_store->loadSpec(m_currentAssetId);
    if (spec.assetId.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("缺少素材规格。"));
        return;
    }
    if (rec.lockedFrames.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先选择候选图并点击「标准化并锁定」。"));
        return;
    }

    // 收集锁定帧
    const QString exportName = rec.name.isEmpty() ? rec.id : rec.name;

    // 合并检查报告：确定性硬性检查 + 视觉启发式审查
    auto runAllChecks = [this, &spec](const QVector<DeterministicChecks::FrameEntry>& frames)
            -> ReviewReport {
        ReviewReport det = DeterministicChecks::run(spec, frames);
        QVector<VisualHeuristics::FrameEntry> visFrames;
        for (const DeterministicChecks::FrameEntry& f : frames) {
            VisualHeuristics::FrameEntry vf;
            vf.dir = f.dir;
            vf.image = f.image;
            visFrames.append(vf);
        }
        const ReviewReport vis = VisualHeuristics::run(spec, visFrames);
        det.issues += vis.issues;
        det.score = (det.score + vis.score) / 2.0;
        det.passed = det.passed && vis.passed;
        det.stage = QStringLiteral("deterministic+vision-heuristic");
        m_store->saveReviewReport(m_currentAssetId, det);
        return det;
    };

    if (spec.type == AssetType::Character) {
        const Direction order[4] = { Direction::Down, Direction::Up, Direction::Left, Direction::Right };
        QVector<QVector<QImage>> rows;
        QVector<QVector<Direction>> rowDirs;
        QVector<DeterministicChecks::FrameEntry> checkFrames;
        QVector<QRgb> palette;

        for (Direction d : order) {
            const QString key = directionToString(d);
            const QString versionId = rec.lockedFrames.value(key);
            if (versionId.isEmpty())
                continue;
            QString imgPath;
            for (const AssetVersion& v : rec.versions)
                if (v.versionId == versionId)
                    imgPath = m_store->absolutePath(v.filePath);
            QImage frame(imgPath);
            if (frame.isNull()) {
                QMessageBox::warning(this, QStringLiteral("错误"),
                                     QStringLiteral("锁定帧读取失败：%1").arg(imgPath));
                return;
            }
            rows.append(QVector<QImage>{ frame });
            rowDirs.append(QVector<Direction>{ d });
            DeterministicChecks::FrameEntry e;
            e.dir = d;
            e.image = frame;
            checkFrames.append(e);
            if (palette.isEmpty())
                palette = AssetFactory::buildPalette(frame, spec.paletteSize);
        }
        if (rows.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("没有已锁定的方向帧。"));
            return;
        }

        // 确定性检查 + 视觉启发式审查（技术路线 7.2 / 7.3）
        m_lastReport = runAllChecks(checkFrames);

        // 导出图集与元数据（技术路线 11）
        const QImage sheet = AssetFactory::packSheet(rows, spec.frameSize);
        const QString sheetPath = m_store->saveExport(m_currentAssetId,
                                                      exportName + QStringLiteral("-sheet"), sheet);
        QJsonObject meta = AssetFactory::sheetMetadata(spec, rowDirs, spec.frameSize, palette);
        meta.insert(QStringLiteral("assetId"), rec.id);
        meta.insert(QStringLiteral("assetName"), rec.name);
        const QString metaPath = m_store->saveExportJson(m_currentAssetId,
                                                         exportName + QStringLiteral("-sheet"), meta);
        // 预览放大版
        m_store->saveExport(m_currentAssetId, exportName + QStringLiteral("-preview"),
                            AssetFactory::upscaleNearest(sheet, 4));

        appendLog(QStringLiteral("项目存档已保存：%1").arg(
            QDir::toNativeSeparators(m_store->absolutePath(sheetPath))));

        // 用户选择导出目录与格式（技术路线 11：引擎无关 + 目标引擎转换）
        AssetExporter::SheetData data;
        data.sheet = sheet;
        data.frameSize = spec.frameSize;
        for (const QVector<Direction>& rd : rowDirs)
            data.rowDirNames.append(directionToString(rd.first()));
        for (int i = 0; i < rowDirs.size(); ++i)
            data.rowFrameCounts.append(1); // MVP：每方向待机 1 帧
        data.palette = palette;
        data.meta = meta;
        const QStringList userFiles = exportToUserDir(data, exportName);

        QString filesText;
        if (!userFiles.isEmpty()) {
            filesText = QStringLiteral("已导出到你选择的目录：\n");
            for (const QString& f : userFiles)
                filesText += QDir::toNativeSeparators(f) + QLatin1Char('\n');
        } else {
            filesText = QStringLiteral("（未进行外部导出，仅保留项目存档）\n");
        }

        QMessageBox box(this);
        box.setWindowTitle(m_lastReport.passed ? QStringLiteral("导出完成 ✓")
                                               : QStringLiteral("导出完成（存在待人工确认项）"));
        box.setIcon(m_lastReport.passed ? QMessageBox::Information : QMessageBox::Warning);
        box.setText(filesText + QStringLiteral("\n项目存档：%1\n%2\n\n%3")
                        .arg(QDir::toNativeSeparators(m_store->absolutePath(sheetPath)),
                             QDir::toNativeSeparators(m_store->absolutePath(metaPath)),
                             m_lastReport.toDisplayText()));
        box.setTextFormat(Qt::PlainText);
        box.exec();
        return;
    }

    // ---- 物品 / 场景：单帧导出 ----
    const QString versionId = rec.lockedFrames.value(QStringLiteral("single"));
    QString imgPath;
    for (const AssetVersion& v : rec.versions)
        if (v.versionId == versionId)
            imgPath = m_store->absolutePath(v.filePath);
    QImage frame(imgPath);
    if (frame.isNull()) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("锁定帧读取失败。"));
        return;
    }

    QVector<DeterministicChecks::FrameEntry> checkFrames;
    DeterministicChecks::FrameEntry e;
    e.image = frame;
    checkFrames.append(e);
    m_lastReport = runAllChecks(checkFrames);

    const QString outPath = m_store->saveExport(m_currentAssetId, exportName, frame);
    QJsonObject meta;
    meta.insert(QStringLiteral("assetId"), rec.id);
    meta.insert(QStringLiteral("assetName"), rec.name);
    meta.insert(QStringLiteral("assetType"), assetTypeToString(spec.type));
    meta.insert(QStringLiteral("width"), frame.width());
    meta.insert(QStringLiteral("height"), frame.height());
    meta.insert(QStringLiteral("anchor"), spec.anchor);
    const QString metaPath = m_store->saveExportJson(m_currentAssetId, exportName, meta);

    appendLog(QStringLiteral("项目存档已保存：%1").arg(
        QDir::toNativeSeparators(m_store->absolutePath(outPath))));

    // 用户选择导出目录与格式
    AssetExporter::SheetData data;
    data.sheet = frame;
    data.frameSize = qMin(frame.width(), frame.height());
    data.rowDirNames.append(QStringLiteral("single"));
    data.rowFrameCounts.append(1);
    data.palette = AssetFactory::buildPalette(frame, spec.paletteSize);
    data.meta = meta;
    const QStringList userFiles = exportToUserDir(data, exportName);

    QString filesText;
    if (!userFiles.isEmpty()) {
        filesText = QStringLiteral("已导出到你选择的目录：\n");
        for (const QString& f : userFiles)
            filesText += QDir::toNativeSeparators(f) + QLatin1Char('\n');
    } else {
        filesText = QStringLiteral("（未进行外部导出，仅保留项目存档）\n");
    }

    QMessageBox box(this);
    box.setWindowTitle(m_lastReport.passed ? QStringLiteral("导出完成 ✓")
                                           : QStringLiteral("导出完成（存在待人工确认项）"));
    box.setIcon(m_lastReport.passed ? QMessageBox::Information : QMessageBox::Warning);
    box.setText(filesText + QStringLiteral("\n项目存档：%1\n%2\n\n%3")
                    .arg(QDir::toNativeSeparators(m_store->absolutePath(outPath)),
                         QDir::toNativeSeparators(m_store->absolutePath(metaPath)),
                         m_lastReport.toDisplayText()));
    box.setTextFormat(Qt::PlainText);
    box.exec();
}

QStringList MainWindow::exportToUserDir(const AssetExporter::SheetData& data,
                                        const QString& defaultBaseName)
{
    ExportDialog dlg(this, defaultBaseName);
    if (dlg.exec() != QDialog::Accepted)
        return {};
    QString err;
    const QStringList files = AssetExporter::exportSheet(
        data, dlg.format(), dlg.targetDir(), dlg.baseName(), &err);
    if (files.isEmpty()) {
        appendLog(QStringLiteral("导出失败：%1").arg(err));
        QMessageBox::warning(this, QStringLiteral("导出失败"), err);
        return {};
    }
    QSettings settings;
    settings.setValue(QStringLiteral("export/dir"), dlg.targetDir());
    for (const QString& f : files)
        appendLog(QStringLiteral("已导出：%1").arg(QDir::toNativeSeparators(f)));
    return files;
}

void MainWindow::onShowReport()
{
    if (m_lastReport.issues.isEmpty() && !m_lastReport.passed) {
        QMessageBox::information(this, QStringLiteral("检查报告"),
                                 QStringLiteral("尚无报告：请先锁定帧并导出。"));
        return;
    }
    QMessageBox::information(this, QStringLiteral("检查报告"), m_lastReport.toDisplayText());
}

// ---------------------------------------------------------------- helpers

void MainWindow::rebuildAssetStatus()
{
    AssetRecord rec = currentRecord();
    if (rec.id.isEmpty()) {
        m_assetStatusLabel->setText(QStringLiteral("当前素材：（无）— 输入提示词开始生成"));
        return;
    }
    QString masterTxt = rec.masterVersionId.isEmpty()
                            ? QStringLiteral("未设定")
                            : QStringLiteral("已设定");
    QString lockTxt;
    if (rec.type == AssetType::Character) {
        static const char* keys[4] = { "down", "up", "left", "right" };
        static const QString names[4] = { QStringLiteral("下"), QStringLiteral("上"),
                                          QStringLiteral("左"), QStringLiteral("右") };
        for (int i = 0; i < 4; ++i)
            lockTxt += QStringLiteral("%1:%2  ")
                           .arg(names[i],
                                rec.lockedFrames.contains(QLatin1String(keys[i]))
                                    ? QStringLiteral("✓") : QStringLiteral("—"));
    } else {
        lockTxt = rec.lockedFrames.contains(QStringLiteral("single"))
                      ? QStringLiteral("已锁定") : QStringLiteral("未锁定");
    }
    m_assetStatusLabel->setText(QStringLiteral(
        "当前素材：%1\n母版：%2   锁定帧：%3")
        .arg(rec.name, masterTxt, lockTxt));
}

void MainWindow::setUiBusy(bool busy)
{
    m_generateBtn->setEnabled(!busy);
    m_cancelBtn->setEnabled(busy);
    m_typeCombo->setEnabled(!busy);
    m_candidateSpin->setEnabled(!busy);
    m_frameSizeCombo->setEnabled(!busy);
    m_modeCombo->setEnabled(!busy);
}

void MainWindow::appendLog(const QString& line)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_logEdit->appendPlainText(QStringLiteral("[%1] %2").arg(stamp, line));
}

} // namespace gc
