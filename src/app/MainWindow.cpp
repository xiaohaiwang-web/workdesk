#include "MainWindow.h"
#include "workdesk/core/FileManager.h"
#include "workdesk/core/DatabaseManager.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QDebug>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QUrl>
#include <QDesktopServices>
#include <QDir>
#include <QLabel>
#include <QFileInfo>
#include <QMessageBox>
#include <QFileIconProvider>
#include <QEasingCurve>
#include <QTimer>
#include <QStandardPaths> 
#include <QMenu>
#include <QApplication>
#include <QSettings>
#include <QProcess>
#include <QDialog>
#include <QDialogButtonBox>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("WorkDesk - 智能文件管家");
    resize(1100, 700);
    setAcceptDrops(true);

    dataDir = qEnvironmentVariable("ProgramData") + "/WorkDesk";
    if (dataDir == "/WorkDesk") { 
        dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/WorkDesk";
    }
    QDir().mkpath(dataDir);
    DatabaseManager::instance().init(dataDir + "/workdesk.db");

    QWidget* central = new QWidget(this);
    QHBoxLayout* mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ================= 左侧：桌面区 =================
    QWidget* leftWidget = new QWidget(central);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftWidget->setStyleSheet("background-color: #F3F3F3;");
    
    QHBoxLayout* leftHeaderLayout = new QHBoxLayout();
    leftHeaderLayout->setContentsMargins(10, 5, 10, 5);
    QLabel* titleLabel = new QLabel("我的桌面 (拖拽文件到这里)", leftWidget);
    titleLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    
    refreshBtn = new QPushButton("刷新", leftWidget);
    refreshBtn->setStyleSheet("QPushButton { border: 1px solid #CCC; border-radius: 3px; padding: 2px 8px; background: #FFF; } "
                              "QPushButton:hover { background: #E0E0E0; }");
    
    leftHeaderLayout->addWidget(titleLabel);
    leftHeaderLayout->addStretch();
    leftHeaderLayout->addWidget(refreshBtn);

    QWidget* headerWidget = new QWidget(leftWidget);
    headerWidget->setLayout(leftHeaderLayout);
    headerWidget->setStyleSheet("background-color: #E8E8E8; border-bottom: 1px solid #CCC;");
    
    fileGrid = new QListWidget(leftWidget);
    fileGrid->setViewMode(QListView::IconMode);
    fileGrid->setResizeMode(QListView::Adjust);
    fileGrid->setGridSize(QSize(120, 120));
    fileGrid->setIconSize(QSize(64, 64));
    fileGrid->setWordWrap(true);
    fileGrid->setSpacing(10);
    fileGrid->setMovement(QListView::Snap);
    fileGrid->setContextMenuPolicy(Qt::CustomContextMenu);
    fileGrid->setStyleSheet("QListWidget { background: transparent; border: none; } "
                            "QListWidget::item { color: #333; } "
                            "QListWidget::item:selected { background: #CCE8FF; border-radius: 5px; }");

    leftLayout->addWidget(headerWidget);
    leftLayout->addWidget(fileGrid);

    // ================= 中间：拉手 =================
    ropeButton = new QPushButton("≡", central);
    ropeButton->setFixedWidth(20);
    ropeButton->setCursor(Qt::PointingHandCursor);
    ropeButton->setStyleSheet(
        "QPushButton { background-color: #DDD; border: none; border-left: 1px solid #CCC; border-right: 1px solid #CCC; color: #666; font-weight: bold; font-size: 16px; }"
        "QPushButton:hover { background-color: #CCC; }"
    );

    // ================= 右侧：AI 助手区 =================
    rightWidget = new QWidget(central);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(10, 10, 10, 10);
    rightWidget->setStyleSheet("background-color: #FAFAFA;");
    
    QHBoxLayout* headerAILayout = new QHBoxLayout();
    QLabel* aiLabel = new QLabel("AI 助手", rightWidget);
    aiLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    
    endChatBtn = new QPushButton("结束对话", rightWidget);
    endChatBtn->setStyleSheet("QPushButton { color: #D32F2F; border: 1px solid #D32F2F; border-radius: 3px; padding: 2px 8px; } "
                              "QPushButton:hover { background-color: #FFCDD2; }");
    
    headerAILayout->addWidget(aiLabel);
    headerAILayout->addStretch();
    headerAILayout->addWidget(endChatBtn);
    
    chatHistory = new QTextEdit(rightWidget);
    chatHistory->setReadOnly(true);
    
    chatInput = new QLineEdit(rightWidget);
    chatInput->setPlaceholderText("输入指令寻找文件，回车...");
    chatInput->setMinimumHeight(35);
    
    rightLayout->addLayout(headerAILayout);
    rightLayout->addWidget(chatHistory);
    rightLayout->addWidget(chatInput);

    rightWidget->setMaximumWidth(400);
    rightWidget->setMinimumWidth(400);
    isAIPanelOpen = true;

    mainLayout->addWidget(leftWidget, 1);
    mainLayout->addWidget(ropeButton);
    mainLayout->addWidget(rightWidget);
    setCentralWidget(central);

    connect(chatInput, &QLineEdit::returnPressed, this, &MainWindow::onAIChatSubmitted);
    connect(fileGrid, &QListWidget::itemClicked, this, &MainWindow::onFileItemClicked);
    connect(ropeButton, &QPushButton::clicked, this, &MainWindow::toggleAIPanel);
    connect(endChatBtn, &QPushButton::clicked, this, &MainWindow::onEndConversation);
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshDesktop);
    connect(fileGrid, &QListWidget::customContextMenuRequested, this, &MainWindow::onFileItemContextMenu);

    animation = new QPropertyAnimation(rightWidget, "maximumWidth", this);
    animation->setDuration(300);
    animation->setEasingCurve(QEasingCurve::InOutQuad);

    refreshDesktop();
    setupAI(); 
    setupTray(); 
}

MainWindow::~MainWindow() {
    if (aiThread) {
        aiThread->quit();
        aiThread->wait(3000);
    }
}

// 【新增】搜索结果弹窗
void MainWindow::showSearchResultsDialog(const QList<FileRecord>& results) {
    if (results.isEmpty()) return;

    QDialog dialog(this);
    dialog.setWindowTitle("搜索结果");
    dialog.resize(400, 300);
    
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QLabel* tip = new QLabel(QString("为你找到 %1 个相关文件（双击打开）：").arg(results.size()), &dialog);
    layout->addWidget(tip);

    QListWidget* list = new QListWidget(&dialog);
    QFileIconProvider iconProvider;
    for (const auto& r : results) {
        QListWidgetItem* item = new QListWidgetItem(r.fileName);
        item->setIcon(iconProvider.icon(QFileInfo(r.filePath)));
        item->setData(Qt::UserRole, r.filePath);
        list->addItem(item);
    }
    layout->addWidget(list);

    // 双击列表项直接打开文件
    connect(list, &QListWidget::itemDoubleClicked, [&dialog](QListWidgetItem* item) {
        QString path = item->data(Qt::UserRole).toString();
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        dialog.accept();
    });

    dialog.exec();
}

void MainWindow::onFileItemContextMenu(const QPoint& pos) {
    QListWidgetItem* item = fileGrid->itemAt(pos);
    if (!item) return;

    QString filePath = item->data(Qt::UserRole).toString();

    QMenu menu(this);
    QAction* openFolderAction = menu.addAction("打开所在文件夹");
    QAction* reextractAction = menu.addAction("重新提取摘要");
    menu.addSeparator();
    QAction* deleteAction = menu.addAction("彻底删除文件");
    
    deleteAction->setIcon(QApplication::style()->standardIcon(QStyle::SP_TrashIcon));

    QAction* selected = menu.exec(fileGrid->mapToGlobal(pos));

    if (selected == openFolderAction) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(filePath).absolutePath()));
    } 
    else if (selected == deleteAction) {
        QMessageBox::StandardButton reply = QMessageBox::question(this, "确认删除", 
            "确定要从电脑中彻底删除文件 \"" + item->text() + "\" 吗？\n此操作不可恢复！", 
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            QFile::remove(filePath);
            DatabaseManager::instance().deleteRecord(filePath);
            refreshDesktop();
        }
    }
    else if (selected == reextractAction) {
        QString summary = FileManager::extractSummary(filePath);
        FileRecord record;
        record.fileName = QFileInfo(filePath).fileName();
        record.filePath = filePath;
        record.summary = summary;
        record.category = FileManager::getCategory(filePath);
        DatabaseManager::instance().addRecord(record);
        refreshDesktop();
        QMessageBox::information(this, "更新成功", "该文件的摘要已重新提取完毕。");
    }
}

// 【重构】核心搜索逻辑
void MainWindow::onAIChatSubmitted() {
    lastUserInput = chatInput->text().trimmed();
    if (lastUserInput.isEmpty()) return;

    chatHistory->append("<b>你:</b> " + lastUserInput);
    chatInput->clear();

    // 1. 先做本地直接搜索保底（防 AI 幻觉）
    // 用户输入“找一下sql作业”，如果文件名里有“sql”，直接就能搜到！
    QString cleanKeyword = lastUserInput;
    cleanKeyword.replace("找一下", "").replace("帮我", "").replace("查找", "").trimmed();
    
    QList<FileRecord> directResults = DatabaseManager::instance().searchFiles(cleanKeyword);
    if (!directResults.isEmpty()) {
        chatHistory->append("<b>AI:</b> 为你找到以下文件，请在弹出的窗口中选择：");
        showSearchResultsDialog(directResults);
        return;
    }

    // 2. 如果直接搜索没搜到，再让 AI 提取核心关键词
    QList<FileRecord> records = DatabaseManager::instance().getAllRecords();
    QString fileListStr;
    for (const auto& r : records) {
        fileListStr += "文件：" + r.fileName + " | 摘要：" + r.summary.left(20) + "\n";
    }

    // 【核心改动】只让 AI 提取词语
    QString prompt = "<|im_start|>system\n"
                     "你是一个关键词提取工具。用户想找文件，请根据用户的描述，在下面的文件列表中寻找最匹配的关键词（比如文件名里的某几个字，或者摘要里的内容）。\n"
                     "如果找不到任何相关的词，回复“无”。\n"
                     "用户文件列表参考：\n" + fileListStr +
                     "<|im_end|>\n"
                     "<|im_start|>user\n" + lastUserInput + "<|im_end|>\n"
                     "<|im_start|>assistant\n";

    QMetaObject::invokeMethod(aiEngine, "processPrompt", Qt::QueuedConnection, Q_ARG(QString, prompt));
}

void MainWindow::setupTray() {
    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));
    trayIcon->setToolTip("WorkDesk - 智能文件管家");

    QMenu* trayMenu = new QMenu(this);
    
    QAction* showAction = new QAction("显示主界面", this);
    connect(showAction, &QAction::triggered, this, [this]() {
        this->showNormal();
        this->activateWindow();
    });

    QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    bool isAutoStart = settings.contains("WorkDesk");
    
    QAction* autoStartAction = new QAction("开机自启动", this);
    autoStartAction->setCheckable(true);
    autoStartAction->setChecked(isAutoStart);
    connect(autoStartAction, &QAction::triggered, this, [this](bool checked) {
        setAutoStart(checked);
    });

    QAction* quitAction = new QAction("彻底退出", this);
    // 【修复】设置标志位，然后再退出
    connect(quitAction, &QAction::triggered, this, [this]() {
        isQuitting = true;
        qApp->quit();
    });

    trayMenu->addAction(showAction);
    trayMenu->addSeparator();
    trayMenu->addAction(autoStartAction);
    trayMenu->addSeparator();
    trayMenu->addAction(quitAction);

    trayIcon->setContextMenu(trayMenu);
    
    connect(trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick) {
            this->showNormal();
            this->activateWindow();
        }
    });

    trayIcon->show();
}

void MainWindow::setAutoStart(bool enable) {
    QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    if (enable) {
        QString appPath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        settings.setValue("WorkDesk", appPath);
    } else {
        settings.remove("WorkDesk");
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // 【修复】如果是点击了彻底退出，直接放行
    if (isQuitting) {
        event->accept();
        return;
    }

    if (trayIcon->isVisible()) {
        hide();
        event->ignore();
        // 延迟一点点弹气泡，效果更好
        QTimer::singleShot(100, this, [this]() {
            trayIcon->showMessage("WorkDesk 正在后台运行", "程序已最小化到托盘，随时为您服务。", QSystemTrayIcon::Information, 2000);
        });
    } else {
        event->accept();
    }
}

void MainWindow::setupAI() {
    aiThread = new QThread(this);
    aiEngine = new AIEngine();
    aiEngine->moveToThread(aiThread);

    connect(aiThread, &QThread::finished, aiEngine, &QObject::deleteLater);
    
    connect(aiEngine, &AIEngine::modelLoaded, this, [this](bool success) {
        if (success) chatHistory->append("<b>系统:</b> AI 模型加载完毕，随时准备为您服务。");
    });
    connect(aiEngine, &AIEngine::thinking, this, [this]() {
        chatHistory->append("<i>AI 正在分析指令...</i>");
    });
    connect(aiEngine, &AIEngine::replyGenerated, this, [this](const QString& reply) {
        QString kw = reply.trimmed();
        if (kw.isEmpty() || kw.contains("无") || kw.length() > 20) {
            chatHistory->append("<b>AI:</b> 抱歉，没有找到相关的文件。");
            return;
        }
        
        // AI 提取了关键词，用关键词去本地数据库搜
        QList<FileRecord> results = DatabaseManager::instance().searchFiles(kw);
        if (results.isEmpty()) {
            chatHistory->append("<b>AI:</b> 没有找到匹配的文件。");
        } else {
            chatHistory->append("<b>AI:</b> 提取到关键词：<b>" + kw + "</b>，为你找到以下文件：");
            showSearchResultsDialog(results);
        }
        
        (void)DatabaseManager::instance().addSearchLog(lastUserInput, kw);
    });
    connect(aiEngine, &AIEngine::errorOccurred, this, [this](const QString& err) {
        chatHistory->append("<b style='color:red;'>错误:</b> " + err);
    });

    aiThread->start();

    QTimer::singleShot(500, this, [this]() {
        QString modelPath = dataDir + "/models/model.gguf";
        QMetaObject::invokeMethod(aiEngine, "loadModel", Qt::QueuedConnection, Q_ARG(QString, modelPath));
    });
}

void MainWindow::toggleAIPanel() {
    animation->stop();
    animation->setStartValue(rightWidget->maximumWidth());
    if (isAIPanelOpen) {
        animation->setEndValue(0);
    } else {
        animation->setEndValue(400);
    }
    animation->start();
    isAIPanelOpen = !isAIPanelOpen;
}

void MainWindow::onEndConversation() {
    chatHistory->clear();
    chatHistory->append("<b>系统:</b> 对话已结束，历史记忆已清空。您可以开始新的寻找任务。");
    QMetaObject::invokeMethod(aiEngine, "resetContext", Qt::QueuedConnection);
}

void MainWindow::refreshDesktop() {
    fileGrid->clear();
    QFileIconProvider iconProvider; 
    QList<FileRecord> records = DatabaseManager::instance().getAllRecords();
    for (const auto& r : records) {
        if (!QFile::exists(r.filePath)) continue; 
        
        QListWidgetItem* item = new QListWidgetItem(fileGrid);
        item->setText(r.fileName);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        item->setIcon(iconProvider.icon(QFileInfo(r.filePath)));
        item->setData(Qt::UserRole, r.filePath);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QMimeData* mimeData = event->mimeData();
    if (mimeData->hasUrls()) {
        for (const QUrl& url : mimeData->urls()) {
            if (url.isLocalFile()) {
                QString originalPath = url.toLocalFile();
                QString newPath = FileManager::classifyAndMove(originalPath, dataDir);
                if (!newPath.isEmpty()) {
                    FileRecord record;
                    record.fileName = QFileInfo(newPath).fileName();
                    record.filePath = newPath;
                    record.summary = FileManager::extractSummary(newPath);
                    record.category = FileManager::getCategory(newPath);
                    DatabaseManager::instance().addRecord(record);
                }
            }
        }
        refreshDesktop();
    }
}

void MainWindow::onFileItemClicked(QListWidgetItem* item) {
    if (!item) return;
    QString path = item->data(Qt::UserRole).toString();
    QString fileName = item->text();

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "打开文件", 
                                  "确定要打开文件 \"" + fileName + "\" 吗？",
                                  QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}