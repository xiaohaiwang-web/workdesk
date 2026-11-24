#include "MainWindow.h"
#include "FileListModel.h"

#include "workdesk/core/DatabaseManager.h"
#include "workdesk/core/FileManager.h"

#include <QApplication>
#include <QAction>
#include <QCoreApplication>
#include <QDateTime>
#include <QTime>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QKeySequence>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QListView>
#include <QVBoxLayout>
#include <QDesktopServices>
#include <QUrl>
#include <QRunnable>
#include <QMetaObject>
#include <QShowEvent>
#include <QAbstractButton>
#include <QToolButton>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#endif

namespace {
QString appDataDirectory()
{
    const QString programData = qEnvironmentVariable("ProgramData");
    if (!programData.isEmpty())
        return QDir(programData).filePath(QStringLiteral("WorkDesk"));
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/WorkDesk");
}

// Keep the UI source compatible with both database API generations:
// Search Core v2 exposes a paged SearchPage, while the earlier core exposes
// searchFiles(). The selected overload is resolved at compile time.
template <typename Database>
auto searchDatabaseRecords(Database& database, const QString& query, int)
    -> decltype(database.searchFilesPage(query, 200, 0).records)
{
    return database.searchFilesPage(query, 200, 0).records;
}

template <typename Database>
auto searchDatabaseRecords(Database& database, const QString& query, long)
    -> decltype(database.searchFiles(query))
{
    return database.searchFiles(query);
}
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // All application exits flow through requestShutdown(), which drains workers
    // and terminates OCR/Office child processes before the event loop stops.
    QApplication::setQuitOnLastWindowClosed(false);

    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setWindowTitle(QStringLiteral("WorkDesk"));
    resize(1440, 900);
    setMinimumSize(1120, 720);
    setAcceptDrops(true);
    setStyleSheet(themeStyleSheet(0.0));

    m_searchPool.setMaxThreadCount(2);
    m_searchPool.setExpiryTimeout(-1);
    m_importPool.setMaxThreadCount(1);
    m_importPool.setExpiryTimeout(-1);

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("WindowSurface"));
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(1, 1, 1, 1);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(createSystemBar());

    auto* body = new QWidget(central);
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addWidget(createSidebar());
    bodyLayout->addWidget(createMainContent(), 1);
    rootLayout->addWidget(body, 1);
    setCentralWidget(central);

    // Theme changes only at coarse 30-minute intervals; there is no animation loop.
    m_themeTimer = new QTimer(this);
    m_themeTimer->setTimerType(Qt::VeryCoarseTimer);
    m_themeTimer->setInterval(30 * 60 * 1000);
    connect(m_themeTimer, &QTimer::timeout, this, &MainWindow::updateThemeForCurrentTime);
    updateThemeForCurrentTime();
    m_themeTimer->start();

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(180);
    connect(m_searchTimer, &QTimer::timeout, this, &MainWindow::runSearch);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() {
        setStatus(QStringLiteral("正在等待输入…"));
        m_searchTimer->start();
    });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        m_searchTimer->stop();
        runSearch();
    });
    auto* focusSearchShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")), this);
    connect(focusSearchShortcut, &QShortcut::activated, this, [this]() {
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    });

    m_dataDir = appDataDirectory();
    if (!QDir().mkpath(m_dataDir)) {
        setStatus(QStringLiteral("无法创建数据目录：%1").arg(m_dataDir));
    }

    m_databaseReady = DatabaseManager::instance().init(
        QDir(m_dataDir).filePath(QStringLiteral("workdesk.db")));
    if (m_engineStatusLabel) {
        m_engineStatusLabel->setText(m_databaseReady
            ? QStringLiteral("●  本地文件库已连接")
            : QStringLiteral("●  本地文件库不可用"));
        m_engineStatusLabel->setProperty("unavailable", !m_databaseReady);
        m_engineStatusLabel->style()->unpolish(m_engineStatusLabel);
        m_engineStatusLabel->style()->polish(m_engineStatusLabel);
    }
    if (!m_databaseReady)
        setStatus(QStringLiteral("数据库初始化失败，请检查数据目录和 SQLite 驱动。"));

    setupTray();
    runSearch();
    if (m_databaseReady)
        indexLegacyFiles();
}

MainWindow::~MainWindow()
{
    m_shutdownRequested.store(true, std::memory_order_relaxed);
    if (m_searchTimer)
        m_searchTimer->stop();
    if (m_themeTimer)
        m_themeTimer->stop();
    m_searchPool.clear();
    m_importPool.clear();
    // requestShutdown() keeps the event loop alive until active work has observed
    // cancellation. These waits should therefore be short and bounded by one page
    // of any currently-running native parser/OCR operation.
    m_searchPool.waitForDone();
    m_importPool.waitForDone();
}

void MainWindow::setCategoryFilter(const QString& category, const QString& label)
{
    m_activeCategory = category;
    m_activeCategoryLabel = label.isEmpty() ? QStringLiteral("全部文件") : label;
    if (category == QStringLiteral("__recent__")) {
        m_proxyModel->setFilterFixedString(QString());
        m_fileModel->sortByRecent();
    } else {
        m_proxyModel->setFilterFixedString(category);
        m_fileModel->restoreDefaultOrder();
    }

    for (auto it = m_navigationButtons.begin(); it != m_navigationButtons.end(); ++it)
        it.value()->setChecked(it.key() == category);

    if (m_listTitleLabel)
        m_listTitleLabel->setText(m_activeCategoryLabel);
    if (m_fileView && m_proxyModel->rowCount() > 0)
        m_fileView->setCurrentIndex(m_proxyModel->index(0, 0));
    else
        updatePreview(QModelIndex());
    setStatus(QStringLiteral("正在显示：%1").arg(m_activeCategoryLabel));
    updateCounts();
    updateContentState();
}

void MainWindow::runSearch()
{
    if (m_isQuitting || !m_searchEdit)
        return;
    if (!m_databaseReady) {
        setStatus(QStringLiteral("数据库不可用，暂时无法搜索。"));
        return;
    }

    const QString query = m_searchEdit->text().trimmed();
    const quint64 generation = ++m_searchGeneration;
    QPointer<MainWindow> self(this);
    const std::atomic_bool* cancel = &m_shutdownRequested;

    setStatus(query.isEmpty() ? QStringLiteral("正在加载文件库…")
                              : QStringLiteral("正在搜索：%1").arg(query));

    m_searchPool.start(QRunnable::create([self, generation, query, cancel]() {
        if (!self || cancel->load(std::memory_order_relaxed)
            || generation != self->m_searchGeneration.load())
            return;

        QList<FileRecord> results;
        if (query.isEmpty())
            results = DatabaseManager::instance().getAllRecords();
        else
            results = searchDatabaseRecords(DatabaseManager::instance(), query, 0);
        if (!self || cancel->load(std::memory_order_relaxed))
            return;
        QMetaObject::invokeMethod(self.data(), [self, generation, query, results]() {
            if (self && !self->m_isQuitting)
                self->applySearchResults(generation, query, results);
        }, Qt::QueuedConnection);
    }));
}

void MainWindow::applySearchResults(quint64 generation, const QString& query,
                                    const QList<FileRecord>& results)
{
    if (generation != m_searchGeneration.load())
        return;

    m_fileModel->setRecords(results);
    if (m_fileView && m_proxyModel->rowCount() > 0)
        m_fileView->setCurrentIndex(m_proxyModel->index(0, 0));
    else
        updatePreview(QModelIndex());
    updateCounts();
    updateContentState();

    if (query.isEmpty())
        setStatus(QStringLiteral("文件库已更新 · %1 项记录").arg(results.size()));
    else
        setStatus(QStringLiteral("搜索完成 · 找到 %1 项结果").arg(results.size()));
}

void MainWindow::updateCounts()
{
    if (!m_fileCountLabel || !m_proxyModel)
        return;
    const int visibleCount = m_proxyModel->rowCount();
    m_fileCountLabel->setText(QStringLiteral("%1 项").arg(visibleCount));
    if (m_resultCountLabel)
        m_resultCountLabel->setText(QStringLiteral("%1 项").arg(visibleCount));
}

void MainWindow::updateContentState()
{
    if (!m_contentStack || !m_proxyModel)
        return;

    const int visibleCount = m_proxyModel->rowCount();
    if (visibleCount > 0) {
        m_contentStack->setCurrentIndex(0);
        return;
    }

    const bool hasAnyRecords = m_fileModel && m_fileModel->recordCount() > 0;
    const bool searching = m_searchEdit && !m_searchEdit->text().trimmed().isEmpty();
    if (hasAnyRecords || searching) {
        m_emptyTitleLabel->setText(searching ? QStringLiteral("没有找到匹配文件")
                                             : QStringLiteral("这个分类下还没有文件"));
        m_emptyDescriptionLabel->setText(searching ? QStringLiteral("试试更短的关键词，或检查文件名称和内容。")
                                                   : QStringLiteral("将对应类型的文件拖入窗口，即可自动分类收纳。"));
    } else {
        m_emptyTitleLabel->setText(QStringLiteral("还没有文件"));
        m_emptyDescriptionLabel->setText(QStringLiteral("把文件拖到这个窗口，开始建立你的本地文件库。"));
    }
    m_contentStack->setCurrentIndex(1);
}

void MainWindow::setStatus(const QString& message)
{
    if (m_statusLabel)
        m_statusLabel->setText(message);
}

void MainWindow::updatePreview(const QModelIndex& proxyIndex)
{
    if (!m_detailNameLabel || !m_detailPathLabel || !m_detailCategoryLabel
        || !m_detailSummaryLabel || !m_detailExtensionLabel || !m_openSelectedButton) {
        return;
    }

    if (!proxyIndex.isValid()) {
        m_detailNameLabel->setText(QStringLiteral("尚未选择文件"));
        m_detailPathLabel->setText(QStringLiteral("选择文件后显示本地路径。"));
        m_detailCategoryLabel->setText(QStringLiteral("等待选择"));
        m_detailSummaryLabel->setText(QStringLiteral("选择一条搜索结果，查看已建立索引的内容摘要。"));
        m_detailExtensionLabel->setText(QStringLiteral("—"));
        if (m_detailMetaLabel)
            m_detailMetaLabel->setText(QStringLiteral("选择一个项目查看详细信息"));
        m_openSelectedButton->setEnabled(false);
        return;
    }

    const QString fileName = proxyIndex.data(Qt::DisplayRole).toString();
    const QString category = proxyIndex.data(FileListModel::CategoryRole).toString();
    const QString filePath = proxyIndex.data(FileListModel::FilePathRole).toString();
    const QString summary = proxyIndex.data(FileListModel::SummaryRole).toString().trimmed();
    const QString extension = proxyIndex.data(FileListModel::ExtensionRole).toString().toUpper();

    m_detailNameLabel->setText(fileName);
    m_detailPathLabel->setText(QDir::toNativeSeparators(filePath));
    m_detailCategoryLabel->setText(category.isEmpty() ? QStringLiteral("未分类") : category);
    m_detailSummaryLabel->setText(summary.isEmpty()
        ? QStringLiteral("当前文件尚未提取到可用摘要。可以通过右键菜单重新提取。")
        : summary);
    m_detailExtensionLabel->setText(extension.isEmpty()
        ? QStringLiteral("FILE") : extension.left(4));
    if (m_detailMetaLabel)
        m_detailMetaLabel->setText(QStringLiteral("保存在此设备 · %1").arg(category));
    m_openSelectedButton->setEnabled(!filePath.isEmpty());
}

void MainWindow::importFiles(const QStringList& filePaths)
{
    if (m_isQuitting || filePaths.isEmpty())
        return;
    if (!m_databaseReady) {
        setStatus(QStringLiteral("数据库不可用，无法建立文件索引。"));
        return;
    }

    const QString targetDirectory = m_dataDir;
    QPointer<MainWindow> self(this);
    const std::atomic_bool* cancel = &m_shutdownRequested;
    setStatus(QStringLiteral("正在后台整理 %1 个文件…").arg(filePaths.size()));

    m_importPool.start(QRunnable::create([self, filePaths, targetDirectory, cancel]() {
        QList<FileRecord> importedRecords;
        importedRecords.reserve(4);
        int failedCount = 0;
        int queuedCount = 0;
        int savedCount = 0;

        for (const QString& path : filePaths) {
            if (cancel->load(std::memory_order_relaxed))
                break;
            const QFileInfo sourceInfo(path);
            if (!sourceInfo.exists() || !sourceInfo.isFile()) {
                ++failedCount;
                continue;
            }

            const QString storedPath = FileManager::classifyAndMove(path, targetDirectory);
            if (storedPath.isEmpty()) {
                ++failedCount;
                continue;
            }

            FileRecord record{};
            record.fileName = QFileInfo(storedPath).fileName();
            record.filePath = storedPath;
            record.category = FileManager::getCategory(storedPath);
            const QFileInfo storedInfo(storedPath);
            record.fileSize = storedInfo.size();
            record.modifiedAt = storedInfo.lastModified().toString(Qt::ISODateWithMs);
            record.fullText = FileManager::extractContent(storedPath, &record.contentTruncated, cancel);
            if (cancel->load(std::memory_order_relaxed))
                break;
            record.contentExtracted = true;
            record.summary = record.fullText.isEmpty() ? FileManager::noTextSummary(storedPath)
                                                        : FileManager::makeSummary(record.fullText);
            importedRecords.append(record);
            ++queuedCount;

            // Keep transient memory bounded during multi-file imports.
            if (importedRecords.size() >= 4) {
                savedCount += DatabaseManager::instance().addRecords(importedRecords);
                importedRecords.clear();
            }
        }

        if (!importedRecords.isEmpty())
            savedCount += DatabaseManager::instance().addRecords(importedRecords);
        const int totalFailed = failedCount + queuedCount - savedCount;
        if (!self || cancel->load(std::memory_order_relaxed))
            return;
        QMetaObject::invokeMethod(self.data(), [self, totalFailed, savedCount]() {
            if (!self || self->m_isQuitting)
                return;
            self->runSearch();
            self->setStatus(QStringLiteral("整理完成 · 成功 %1 个，失败 %2 个")
                                .arg(savedCount).arg(totalFailed));
        }, Qt::QueuedConnection);
    }));
}

void MainWindow::indexLegacyFiles()
{
    QPointer<MainWindow> self(this);
    const std::atomic_bool* cancel = &m_shutdownRequested;
    m_importPool.start(QRunnable::create([self, cancel]() {
        int indexedCount = 0;
        while (self && !cancel->load(std::memory_order_relaxed)) {
            const QList<FileRecord> pending = DatabaseManager::instance().getFilesNeedingContentIndex(8);
            if (pending.isEmpty())
                break;

            QList<FileRecord> batch;
            batch.reserve(pending.size());
            for (FileRecord record : pending) {
                if (!self || cancel->load(std::memory_order_relaxed))
                    return;
                const QFileInfo info(record.filePath);
                if (info.exists() && info.isFile()) {
                    record.fileSize = info.size();
                    record.modifiedAt = info.lastModified().toString(Qt::ISODateWithMs);
                    record.fullText = FileManager::extractContent(record.filePath, &record.contentTruncated, cancel);
                    if (cancel->load(std::memory_order_relaxed))
                        return;
                    record.summary = record.fullText.isEmpty()
                        ? FileManager::noTextSummary(record.filePath)
                        : FileManager::makeSummary(record.fullText);
                } else {
                    record.fileSize = 0;
                    record.modifiedAt.clear();
                    record.fullText.clear();
                    record.contentTruncated = false;
                    record.summary = FileManager::noTextSummary(record.filePath);
                }
                record.contentExtracted = true;
                batch.append(record);
            }

            const int saved = DatabaseManager::instance().addRecords(batch);
            if (saved != batch.size())
                break;
            indexedCount += saved;
            if (self && indexedCount > 0 && indexedCount % 32 == 0) {
                QMetaObject::invokeMethod(self.data(), [self, indexedCount]() {
                    if (self)
                        self->setStatus(QStringLiteral("正在补全旧文件全文索引 · 已处理 %1 项")
                                            .arg(indexedCount));
                }, Qt::QueuedConnection);
            }
        }

        if (!self || cancel->load(std::memory_order_relaxed))
            return;
        QMetaObject::invokeMethod(self.data(), [self, indexedCount]() {
            if (!self || self->m_isQuitting)
                return;
            if (indexedCount > 0) {
                self->runSearch();
                self->setStatus(QStringLiteral("旧文件全文索引已补全 · %1 项")
                                    .arg(indexedCount));
            }
        }, Qt::QueuedConnection);
    }));
}

void MainWindow::chooseFiles()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, QStringLiteral("添加文件到 WorkDesk"), QDir::homePath(),
        QStringLiteral("所有文件 (*.*)"));
    if (!paths.isEmpty())
        importFiles(paths);
}

void MainWindow::setViewMode(bool compact)
{
    if (!m_fileView)
        return;

    m_fileView->setProperty("compactView", compact);
    if (compact) {
        m_fileView->setViewMode(QListView::ListMode);
        m_fileView->setFlow(QListView::TopToBottom);
        m_fileView->setWrapping(false);
        m_fileView->setGridSize(QSize());
        m_fileView->setSpacing(2);
    } else {
        m_fileView->setViewMode(QListView::IconMode);
        m_fileView->setFlow(QListView::LeftToRight);
        m_fileView->setWrapping(true);
        m_fileView->setGridSize(QSize(158, 150));
        m_fileView->setSpacing(9);
    }

    if (m_gridModeButton)
        m_gridModeButton->setChecked(!compact);
    if (m_listModeButton)
        m_listModeButton->setChecked(compact);
    m_fileView->doItemsLayout();
}

void MainWindow::reextractSummary(const FileRecord& record)
{
    if (m_isQuitting)
        return;
    QPointer<MainWindow> self(this);
    const std::atomic_bool* cancel = &m_shutdownRequested;
    setStatus(QStringLiteral("正在重新提取正文和摘要…"));
    m_importPool.start(QRunnable::create([self, record, cancel]() {
        FileRecord updated = record;
        updated.fullText = FileManager::extractContent(record.filePath, &updated.contentTruncated, cancel);
        if (cancel->load(std::memory_order_relaxed))
            return;
        updated.contentExtracted = true;
        const QFileInfo info(record.filePath);
        updated.fileSize = info.size();
        updated.modifiedAt = info.lastModified().toString(Qt::ISODateWithMs);
        updated.summary = updated.fullText.isEmpty()
            ? FileManager::noTextSummary(record.filePath)
            : FileManager::makeSummary(updated.fullText);
        const bool saved = DatabaseManager::instance().addRecord(updated);
        if (!self || cancel->load(std::memory_order_relaxed))
            return;
        QMetaObject::invokeMethod(self.data(), [self, saved]() {
            if (!self || self->m_isQuitting)
                return;
            self->runSearch();
            self->setStatus(saved ? QStringLiteral("正文和摘要已更新")
                                 : QStringLiteral("提取完成，但索引写入失败"));
        }, Qt::QueuedConnection);
    }));
}

void MainWindow::openRecord(const FileRecord& record)
{
    if (!QFileInfo::exists(record.filePath)) {
        QMessageBox::warning(this, QStringLiteral("文件不存在"),
                             QStringLiteral("原文件可能已被移动或删除：\n%1").arg(record.filePath));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(record.filePath));
}

void MainWindow::onFileActivated(const QModelIndex& index)
{
    if (!index.isValid())
        return;
    const QModelIndex sourceIndex = m_proxyModel->mapToSource(index);
    const FileRecord* record = m_fileModel->recordAt(sourceIndex.row());
    if (record)
        openRecord(*record);
}

void MainWindow::onFileContextMenu(const QPoint& pos)
{
    const QModelIndex proxyIndex = m_fileView->indexAt(pos);
    if (!proxyIndex.isValid())
        return;

    const QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
    const FileRecord* recordPtr = m_fileModel->recordAt(sourceIndex.row());
    if (!recordPtr)
        return;
    const FileRecord record = *recordPtr;

    QMenu menu(this);
    QAction* openAction = menu.addAction(QStringLiteral("打开文件"));
    QAction* folderAction = menu.addAction(QStringLiteral("打开所在文件夹"));
    QAction* copyAction = menu.addAction(QStringLiteral("复制文件路径"));
    menu.addSeparator();
    QAction* reextractAction = menu.addAction(QStringLiteral("重新提取正文和摘要"));
    QAction* deleteAction = menu.addAction(QStringLiteral("从文件库删除并删除原文件"));
    deleteAction->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));

    QAction* selected = menu.exec(m_fileView->viewport()->mapToGlobal(pos));
    if (selected == openAction) {
        openRecord(record);
    } else if (selected == folderAction) {
        if (QFileInfo::exists(record.filePath))
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(record.filePath).absolutePath()));
    } else if (selected == copyAction) {
        QApplication::clipboard()->setText(record.filePath);
        setStatus(QStringLiteral("文件路径已复制"));
    } else if (selected == reextractAction) {
        reextractSummary(record);
    } else if (selected == deleteAction) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("确认删除"),
            QStringLiteral("这会同时删除磁盘上的原文件，且无法撤销。\n\n%1\n\n确定继续吗？")
                .arg(record.filePath),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer == QMessageBox::Yes) {
            if (QFileInfo::exists(record.filePath) && !QFile::remove(record.filePath)) {
                QMessageBox::warning(this, QStringLiteral("删除失败"),
                                     QStringLiteral("无法删除文件，请检查文件是否正在被占用。"));
                return;
            }
            DatabaseManager::instance().deleteRecord(record.filePath);
            runSearch();
        }
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
    }
    if (!paths.isEmpty()) {
        event->acceptProposedAction();
        importFiles(paths);
    }
}

void MainWindow::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(style()->standardIcon(QStyle::SP_DirHomeIcon));
    m_trayIcon->setToolTip(QStringLiteral("WorkDesk - 本地文件库"));

    auto* trayMenu = new QMenu(this);
    QAction* showAction = trayMenu->addAction(QStringLiteral("显示 WorkDesk"));
    connect(showAction, &QAction::triggered, this, [this]() {
        showNormal();
        raise();
        activateWindow();
    });

#ifdef Q_OS_WIN
    trayMenu->addSeparator();
    QSettings autoStartSettings(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
        QSettings::NativeFormat);
    QAction* autoStartAction = trayMenu->addAction(QStringLiteral("开机启动"));
    autoStartAction->setCheckable(true);
    autoStartAction->setChecked(autoStartSettings.contains(QStringLiteral("WorkDesk")));
    connect(autoStartAction, &QAction::toggled, this, &MainWindow::setAutoStart);
#endif

    trayMenu->addSeparator();
    QAction* quitAction = trayMenu->addAction(QStringLiteral("退出 WorkDesk"));
    connect(quitAction, &QAction::triggered, this, &MainWindow::requestShutdown);

    m_trayIcon->setContextMenu(trayMenu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick) {
            showMaximized();
            raise();
            activateWindow();
        }
    });
    m_trayIcon->show();
}


double MainWindow::currentNightWeight() const
{
    const QTime now = QTime::currentTime();
    const int minuteOfDay = now.hour() * 60 + now.minute();

    // Fixed local civil-time schedule (no location or network dependency):
    // dawn 05:00-07:00 and dusk 17:00-19:00, linearly blended.
    constexpr int dawnStart = 5 * 60;
    constexpr int sunrise = 7 * 60;
    constexpr int sunset = 17 * 60;
    constexpr int duskEnd = 19 * 60;

    if (minuteOfDay < dawnStart || minuteOfDay >= duskEnd)
        return 1.0;
    if (minuteOfDay < sunrise)
        return 1.0 - static_cast<double>(minuteOfDay - dawnStart) / (sunrise - dawnStart);
    if (minuteOfDay < sunset)
        return 0.0;
    return static_cast<double>(minuteOfDay - sunset) / (duskEnd - sunset);
}

void MainWindow::updateThemeForCurrentTime()
{
    if (m_isQuitting)
        return;

    const double nightWeight = currentNightWeight();
    setStyleSheet(themeStyleSheet(nightWeight));
    if (m_fileView)
        m_fileView->setProperty("nightWeight", nightWeight);

    if (m_themeIndicatorLabel) {
        const bool night = nightWeight >= 0.5;
        m_themeIndicatorLabel->setText(night
            ? QStringLiteral("☾  夜间 · 自动")
            : QStringLiteral("☀  日间 · 自动"));
        m_themeIndicatorLabel->setToolTip(QStringLiteral(
            "自动主题 · 本地时间 05:00–07:00 日出渐亮，17:00–19:00 日落渐暗"));
    }
    if (m_fileView)
        m_fileView->viewport()->update();
}

void MainWindow::requestShutdown()
{
    if (m_isQuitting)
        return;

    m_isQuitting = true;
    m_shutdownRequested.store(true, std::memory_order_relaxed);

    if (m_searchTimer)
        m_searchTimer->stop();
    if (m_themeTimer)
        m_themeTimer->stop();
    // Keep the tray icon visible while an active native OCR/render operation
    // reaches its cancellation checkpoint. Its tooltip makes the in-progress
    // shutdown visible instead of looking like a failed Exit command.
    if (m_trayIcon && m_trayIcon->isVisible()) {
        m_trayIcon->setToolTip(QStringLiteral("WorkDesk · 正在安全退出…"));
        hide();
    } else {
        setStatus(QStringLiteral("正在安全退出 · 停止后台任务…"));
        setEnabled(false);
    }

    // Drop work that has not started and ask active import/OCR tasks to stop
    // at their next cancellation checkpoint.
    m_searchPool.clear();
    m_importPool.clear();

    if (!m_shutdownTimer) {
        m_shutdownTimer = new QTimer(this);
        m_shutdownTimer->setInterval(100);
        m_shutdownTimer->setTimerType(Qt::CoarseTimer);
        connect(m_shutdownTimer, &QTimer::timeout, this, [this]() {
            if (m_searchPool.activeThreadCount() == 0
                && m_importPool.activeThreadCount() == 0) {
                m_shutdownTimer->stop();
                if (m_trayIcon)
                    m_trayIcon->hide();
                QCoreApplication::exit(0);
            }
        });
    }
    m_shutdownTimer->start();
}

void MainWindow::setAutoStart(bool enable)
{
#ifdef Q_OS_WIN
    QSettings settings(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
        QSettings::NativeFormat);
    if (enable) {
        settings.setValue(QStringLiteral("WorkDesk"),
                          QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
    } else {
        settings.remove(QStringLiteral("WorkDesk"));
    }
#else
    Q_UNUSED(enable);
#endif
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_isQuitting) {
        event->accept();
        return;
    }

    if (!m_trayIcon || !m_trayIcon->isVisible()) {
        // Without a tray icon there is no discoverable way to quit later.
        // Treat the window close as a real shutdown and cancel background work.
        event->ignore();
        requestShutdown();
        return;
    }

    hide();
    event->ignore();
    m_trayIcon->showMessage(QStringLiteral("WorkDesk 仍在运行"),
                            QStringLiteral("程序已最小化到系统托盘。选择“退出 WorkDesk”可完全结束程序。"),
                            QSystemTrayIcon::Information, 1800);
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    if (hwnd) {
        const LONG_PTR currentStyle = GetWindowLongPtr(hwnd, GWL_STYLE);
        if ((currentStyle & WS_THICKFRAME) == 0) {
            SetWindowLongPtr(hwnd, GWL_STYLE, currentStyle | WS_THICKFRAME);
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                         SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
    }
#endif
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    if (eventType == QByteArrayLiteral("windows_generic_MSG")) {
        auto* msg = static_cast<MSG*>(message);
        if (msg && msg->message == WM_NCHITTEST) {
            const QPoint local = mapFromGlobal(QPoint(GET_X_LPARAM(msg->lParam),
                                                       GET_Y_LPARAM(msg->lParam)));
            constexpr int resizeBorder = 8;
            if (!isMaximized()) {
                const bool left = local.x() < resizeBorder;
                const bool right = local.x() >= width() - resizeBorder;
                const bool top = local.y() < resizeBorder;
                const bool bottom = local.y() >= height() - resizeBorder;
                if (top && left) *result = HTTOPLEFT;
                else if (top && right) *result = HTTOPRIGHT;
                else if (bottom && left) *result = HTBOTTOMLEFT;
                else if (bottom && right) *result = HTBOTTOMRIGHT;
                else if (left) *result = HTLEFT;
                else if (right) *result = HTRIGHT;
                else if (top) *result = HTTOP;
                else if (bottom) *result = HTBOTTOM;
                else *result = HTCLIENT;
                if (*result != HTCLIENT)
                    return true;
            }

            if (local.y() >= 0 && local.y() < 43) {
                QWidget* child = childAt(local);
                for (QWidget* current = child; current && current != this;
                     current = current->parentWidget()) {
                    if (qobject_cast<QAbstractButton*>(current))
                        return false;
                    if (current->objectName() == QStringLiteral("SystemBar"))
                        break;
                }
                *result = HTCAPTION;
                return true;
            }
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}
