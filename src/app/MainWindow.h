#pragma once
#include <QMainWindow>
#include <QListWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSystemTrayIcon> 
#include <QCloseEvent>     
#include <QMouseEvent> 
#include "workdesk/core/FileRecord.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override; 
    
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private slots:
    void onAIChatSubmitted();
    void onFileItemClicked(QListWidgetItem* item);
    void toggleAIPanel(); 
    void onEndConversation(); 
    void onFileItemContextMenu(const QPoint& pos); 

private:
    void refreshDesktop();
    void setupTray();       
    void setAutoStart(bool enable); 
    void showSearchResultsDialog(const QList<FileRecord>& results); 
    
    QListWidget* fileGrid;
    
    QWidget* rightWidget;
    QTextEdit* chatHistory;
    QLineEdit* chatInput;
    QPushButton* ropeButton;
    QPushButton* endChatBtn; 
    QPushButton* refreshBtn; 
    
    // 【新增】窗口控制按钮
    QPushButton* btnMinimize;
    QPushButton* btnMaximize;
    QPushButton* btnClose;

    QPropertyAnimation* animation;
    bool isAIPanelOpen = true; 

    QSystemTrayIcon* trayIcon; 

    QString dataDir; 
    QString lastUserInput; 
    bool isQuitting = false; 
    
    QPoint m_dragPosition;
};