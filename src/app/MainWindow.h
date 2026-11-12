#pragma once
#include <QMainWindow>
#include <QListWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QThread>
#include <QSystemTrayIcon> 
#include <QCloseEvent>     
#include "workdesk/core/AIEngine.h"
#include "workdesk/core/FileRecord.h" // 引入数据记录结构

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override; 

private slots:
    void onAIChatSubmitted();
    void onFileItemClicked(QListWidgetItem* item);
    void toggleAIPanel(); 
    void onEndConversation(); 
    void onFileItemContextMenu(const QPoint& pos); 

private:
    void refreshDesktop();
    void setupAI(); 
    void setupTray();       
    void setAutoStart(bool enable); 
    void showSearchResultsDialog(const QList<FileRecord>& results); // 【新增】结果弹窗

    QListWidget* fileGrid;
    
    QWidget* rightWidget;
    QTextEdit* chatHistory;
    QLineEdit* chatInput;
    QPushButton* ropeButton;
    QPushButton* endChatBtn; 
    QPushButton* refreshBtn; 
    QPropertyAnimation* animation;
    bool isAIPanelOpen = true; 

    QThread* aiThread;
    AIEngine* aiEngine;

    QSystemTrayIcon* trayIcon; 

    QString dataDir; 
    QString lastUserInput; 
    bool isQuitting = false; // 【新增】退出标志位
};