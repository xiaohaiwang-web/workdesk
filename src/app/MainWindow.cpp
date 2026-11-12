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
#include <QDialog>
#include <QDialogButtonBox>
#include <QGraphicsDropShadowEffect> 

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("WorkDesk - 本地文件管家");
    resize(1150, 720);
    setAcceptDrops(true);
    
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    
    QWidget* central = new QWidget(this);
    central->setObjectName("centralWidget");
    
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(central);
    shadow->setBlurRadius(25);
    shadow->setColor(QColor(0, 0, 0, 100));
    shadow->setOffset(0, 0);
    central->setGraphicsEffect(shadow);

    central->setStyleSheet(
        "#centralWidget { background-color: #FFFFFF; border-radius: 12px; }"
        "QToolTip { color: #333; background-color: #FFF; border: 1px solid #CCC; }"
    );

    QHBoxLayout* mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ================= 左侧：桌面区 =================
    QWidget* leftWidget = new QWidget(central);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftWidget->setStyleSheet("background-color: #F8F9FA; border-top-left-radius: 12px; border-bottom-left-radius: 12px;");
    
    QWidget* leftHeader = new QWidget(leftWidget);
    leftHeader->setFixedHeight(50);
    leftHeader->setStyleSheet("background-color: #FFFFFF; border-top-left-radius: 12px; border-bottom: 1px solid #EAEAEA;");
    QHBoxLayout* leftHeaderLayout = new QHBoxLayout(leftHeader);
    
    QLabel* titleLabel = new QLabel("我的桌面", leftHeader);
    titleLabel->setStyleSheet("font-weight: 600; font-size: 15px; color: #222;");
    
    refreshBtn = new QPushButton("刷新", leftHeader);
    refreshBtn->setCursor(Qt::PointingHandCursor);
    refreshBtn->setStyleSheet("QPushButton { border: none; color: #0078D7; font-size: 13px; } "
                              "QPushButton:hover { text-decoration: underline; }");
    
    leftHeaderLayout->addWidget(titleLabel);
    leftHeaderLayout->addStretch();
    leftHeaderLayout->addWidget(refreshBtn);

    fileGrid = new QListWidget(leftWidget);
    fileGrid->setViewMode(QListView::IconMode);
    fileGrid->setResizeMode(QListView::Adjust);
    
    // 【核心修复】彻底关闭文本省略，并加大网格宽高
    fileGrid->setTextElideMode(Qt::ElideNone); 
    fileGrid->setGridSize(QSize(120, 140)); // 高度给足，容纳三行文字
    fileGrid->setIconSize(QSize(48, 48));   
    fileGrid->setWordWrap(true);
    fileGrid->setSpacing(10);
    fileGrid->setMovement(QListView::Snap);
    fileGrid->setContextMenuPolicy(Qt::CustomContextMenu);
    
    QFont gridFont = fileGrid->font();
    gridFont.setPointSize(9);
    fileGrid->setFont(gridFont);
    
    fileGrid->setStyleSheet(
        "QListWidget { background: transparent; border: none; outline: none; } "
        "QListWidget::item { color: #444; border-radius: 6px; padding: 5px; } "
        "QListWidget::item:hover { background: #F0F0F0; } "
        "QListWidget::item:selected { background: #E5F3FF; color: #0066CC; }"
    );

    leftLayout->addWidget(leftHeader);
    leftLayout->addWidget(fileGrid);

    // ================= 中间：拉手 =================
    ropeButton = new QPushButton("≡", central);
    ropeButton->setFixedWidth(14);
    ropeButton->setCursor(Qt::PointingHandCursor);
    ropeButton->setStyleSheet(
        "QPushButton { background-color: #F9F9F9; border: none; border-left: 1px solid #EEE; border-right: 1px solid #EEE; color: #BBB; font-weight: bold; }"
        "QPushButton:hover { background-color: #F0F0F0; color: #666; }"
    );

    // ================= 右侧：搜索区 =================
    rightWidget = new QWidget(central);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(15, 10, 15, 15);
    rightWidget->setStyleSheet("background-color: #FAFAFA; border-top-right-radius: 12px; border-bottom-right-radius: 12px;");
    
    QHBoxLayout* headerAILayout = new QHBoxLayout();
    headerAILayout->setContentsMargins(0, 0, 0, 0);
    
    QLabel* aiLabel = new QLabel("极速搜索", rightWidget);
    aiLabel->setStyleSheet("font-weight: 600; font-size: 15px; color: #222;");
    
    endChatBtn = new QPushButton("清空", rightWidget);
    endChatBtn->setCursor(Qt::PointingHandCursor);
    endChatBtn->setStyleSheet("QPushButton { color: #666; border: 1px solid #DDD; border-radius: 10px; padding: 3px 10px; background: #FFF; font-size: 12px; } "
                              "QPushButton:hover { background-color: #F5F5F5; color: #D32F2F; border-color: #D32F2F; }");

    btnMinimize = new QPushButton("—", rightWidget);
    btnMaximize = new QPushButton("□", rightWidget);
    btnClose = new QPushButton("✕", rightWidget);
    
    QString winCtrlStyle = "QPushButton { border: none; font-size: 14px; color: #888; padding: 2px 6px; border-radius: 4px; }"
                           "QPushButton:hover { background-color: #E0E0E0; color: #000; }";
    btnMinimize->setStyleSheet(winCtrlStyle);
    btnMaximize->setStyleSheet(winCtrlStyle);
    btnClose->setStyleSheet("QPushButton { border: none; font-size: 14px; color: #888; padding: 2px 6px; border-radius: 4px; }"
                            "QPushButton:hover { background-color: #E81123; color: #FFF; }");

    connect(btnMinimize, &QPushButton::clicked, this, &MainWindow::showMinimized);
    connect(btnMaximize, &QPushButton::clicked, this, [this]() {
        if (isMaximized()) showNormal(); else showMaximized();
    });
    connect(btnClose, &QPushButton::clicked, this, &MainWindow::close);

    headerAILayout->addWidget(aiLabel);
    headerAILayout->addStretch();
    headerAILayout->addWidget(endChatBtn);
    headerAILayout->addSpacing(15);
    headerAILayout->addWidget(btnMinimize);
    headerAILayout->addWidget(btnMaximize);
    headerAILayout->addWidget(btnClose);
    
    chatHistory = new QTextEdit(rightWidget);
    chatHistory->setReadOnly(true);
    chatHistory->setStyleSheet("QTextEdit { border: none; background: transparent; font-size: 14px; color: #333; }");
    
    chatInput = new QLineEdit(rightWidget);
    chatInput->setPlaceholderText("输入文件名或内容，回车搜索...");
    chatInput->setMinimumHeight(42);
    chatInput->setStyleSheet(
        "QLineEdit { border: 1px solid #E0E0E0; border-radius: 21px; padding: 0 18px; background: #FFF; font-size: 14px; color: #333; } "
        "QLineEdit:focus { border: 1px solid #0078D7; background: #FFFFFF; }"
    );
    
    rightLayout->addLayout(headerAILayout);
    rightLayout->addSpacing(15);
    rightLayout->addWidget(chatHistory);
    rightLayout->addSpacing(10);
    rightLayout->addWidget(chatInput);

    rightWidget->setMaximumWidth(380);
    rightWidget->setMinimumWidth(380);
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

    QPushButton* settingsBtn = new QPushButton("⚙️", rightWidget);
    settingsBtn->setCursor(Qt::PointingHandCursor);
    settingsBtn->setStyleSheet("QPushButton { border: none; font-size: 14px; color: #888; } QPushButton:hover { color: #000; }");
    headerAILayout->insertWidget(2, settingsBtn); 

    connect(settingsBtn, &QPushButton::clicked, this, [this]() {
        QDialog dlg(this);
        dlg.setWindowTitle("系统设置");
        dlg.resize(300, 150);
        QVBoxLayout* layout = new QVBoxLayout(&dlg);
        
        layout->addWidget(new QLabel("大模型 API 地址:"));
        QLineEdit* apiUrlEdit = new QLineEdit(&dlg);
        apiUrlEdit->setText("https://api.deepseek.com/v1/chat/completions");
        layout->addWidget(apiUrlEdit);
        
        layout->addWidget(new QLabel("API Key:"));
        QLineEdit* apiKeyEdit = new QLineEdit(&dlg);
        apiKeyEdit->setEchoMode(QLineEdit::Password);
        QSettings settings("WorkDesk", "Config");
        apiKeyEdit->setText(settings.value("apiKey").toString());
        layout->addWidget(apiKeyEdit);
        
        QDialogButtonBox* btnBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
        layout->addWidget(btnBox);
        
        connect(btnBox, &QDialogButtonBox::accepted, [&]() {
            settings.setValue("apiUrl", apiUrlEdit->text());
            settings.setValue("apiKey", apiKeyEdit->text());
            dlg.accept();
            QMessageBox::information(this, "成功", "配置已保存！");
        });
        connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        
        dlg.exec();
    });

    animation = new QPropertyAnimation(rightWidget, "maximumWidth", this);
    animation->setDuration(250);
    animation->setEasingCurve(QEasingCurve::InOutQuad);

    dataDir = qEnvironmentVariable("ProgramData") + "/WorkDesk";
    if (dataDir == "/WorkDesk") { 
        dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/WorkDesk";
    }
    QDir().mkpath(dataDir);
    DatabaseManager::instance().init(dataDir + "/workdesk.db");

    refreshDesktop();
    setupTray(); 
}

MainWindow::~MainWindow() {}

void MainWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && event->pos().y() < 50) {
        m_dragPosition = event->globalPos() - frameGeometry().topLeft();
        event->accept();
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton && event->pos().y() < 50) {
        move(event->globalPos() - m_dragPosition);
        event->accept();
    }
}

void MainWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        event->accept();
    }
}

void MainWindow::showSearchResultsDialog(const QList<FileRecord>& results) {
    if (results.isEmpty()) return;

    QDialog dialog(this);
    dialog.setWindowTitle("搜索结果");
    dialog.resize(400, 300);
    dialog.setStyleSheet("QDialog { background-color: #FFF; } QListWidget { border: 1px solid #EAEAEA; outline: none; } QListWidget::item { padding: 10px; border-bottom: 1px solid #F5F5F5; } QListWidget::item:selected { background: #E5F3FF; color: #0066CC; }");
    
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
    menu.setStyleSheet("QMenu { background-color: #FFF; border: 1px solid #DDD; border-radius: 6px; padding: 4px; } QMenu::item { padding: 6px 24px; border-radius: 4px; } QMenu::item:selected { background: #F0F0F0; }");
    
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
        if (QMessageBox::question(this, "确认删除", "确定要彻底删除文件吗？", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            QFile::remove(filePath);
            DatabaseManager::instance().deleteRecord(filePath);
            refreshDesktop();
        }
    }
    else if (selected == reextractAction) {
        FileRecord record;
        record.fileName = QFileInfo(filePath).fileName();
        record.filePath = filePath;
        record.summary = FileManager::extractSummary(filePath);
        record.category = FileManager::getCategory(filePath);
        DatabaseManager::instance().addRecord(record);
        refreshDesktop();
        QMessageBox::information(this, "更新成功", "摘要已重新提取。");
    }
}

void MainWindow::onAIChatSubmitted() {
    lastUserInput = chatInput->text().trimmed();
    if (lastUserInput.isEmpty()) return;

    chatHistory->append("<div style='margin-bottom: 10px;'><b>你:</b> " + lastUserInput + "</div>");
    chatInput->clear();

    QString cleanKeyword = lastUserInput;
    QStringList stopWords = {
        "帮我", "找一下", "找一个", "帮我找", "查一下", "查查", "查找", "那个", "这个", "的文件", "文件", "关于", "有关"
    };
    for (const QString& word : stopWords) {
        cleanKeyword.replace(word, " ");
    }
    cleanKeyword.replace("的", " ").trimmed();

    QStringList words = cleanKeyword.split(" ", Qt::SkipEmptyParts);
    if (words.isEmpty()) {
        chatHistory->append("<div style='color: #888;'><b>系统:</b> 请输入要搜索的文件名或内容。</div>");
        return;
    }

    QString finalQuery = words.join(" ");
    QList<FileRecord> results = DatabaseManager::instance().searchFiles(finalQuery);
    
    if (results.isEmpty()) {
        results = DatabaseManager::instance().searchFiles(lastUserInput);
    }

    if (results.isEmpty()) {
        chatHistory->append("<div style='color: #888;'><b>系统:</b> 抱歉，没有找到包含“" + finalQuery + "”的相关文件。</div>");
    } else {
        chatHistory->append("<div style='color: #0078D7; margin-bottom: 10px;'><b>系统:</b> 极速匹配到 " + QString::number(results.size()) + " 个文件：</div>");
        showSearchResultsDialog(results);
    }
}

void MainWindow::setupTray() {
    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));
    trayIcon->setToolTip("WorkDesk - 本地文件管家");

    QMenu* trayMenu = new QMenu(this);
    QAction* showAction = new QAction("显示主界面", this);
    connect(showAction, &QAction::triggered, this, [this]() {
        this->showNormal(); this->activateWindow();
    });

    QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    QAction* autoStartAction = new QAction("开机自启动", this);
    autoStartAction->setCheckable(true);
    autoStartAction->setChecked(settings.contains("WorkDesk"));
    connect(autoStartAction, &QAction::triggered, this, &MainWindow::setAutoStart);

    QAction* quitAction = new QAction("彻底退出", this);
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
            this->showNormal(); this->activateWindow();
        }
    });
    trayIcon->show();
}

void MainWindow::setAutoStart(bool enable) {
    QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    if (enable) {
        settings.setValue("WorkDesk", QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
    } else {
        settings.remove("WorkDesk");
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (isQuitting) { event->accept(); return; }
    if (trayIcon->isVisible()) {
        hide(); event->ignore();
        QTimer::singleShot(100, this, [this]() {
            trayIcon->showMessage("WorkDesk 正在后台运行", "程序已最小化到托盘。", QSystemTrayIcon::Information, 2000);
        });
    } else { event->accept(); }
}

void MainWindow::toggleAIPanel() {
    animation->stop();
    animation->setStartValue(rightWidget->maximumWidth());
    animation->setEndValue(isAIPanelOpen ? 0 : 380);
    animation->start();
    isAIPanelOpen = !isAIPanelOpen;
}

void MainWindow::onEndConversation() {
    chatHistory->clear();
    chatHistory->append("<div style='color: #888;'><b>系统:</b> 记录已清空。</div>");
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
        item->setToolTip(r.fileName); // 【新增】鼠标悬停显示完整文件名
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
    if (QMessageBox::question(this, "打开文件", "确定要打开 " + item->text() + " 吗？", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}