#pragma once

#include <QMainWindow>
#include <QByteArray>
#include <QThreadPool>
#include <QSystemTrayIcon>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QHash>
#include <QStringList>
#include <QList>
#include <QPoint>
#include <atomic>

#include "workdesk/core/FileRecord.h"

class QListView;
class QLineEdit;
class QLabel;
class QPushButton;
class QToolButton;
class QStackedWidget;
class QCloseEvent;
class QShowEvent;
class QDragEnterEvent;
class QDropEvent;
class QModelIndex;
class FileListModel;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    static QString themeStyleSheet(double nightWeight);
    QWidget* createSystemBar();
    QWidget* createSidebar();
    QWidget* createMainContent();
    QPushButton* createNavigationButton(const QString& label, const QString& category);

    void runSearch();
    void applySearchResults(quint64 generation, const QString& query,
                            const QList<FileRecord>& results);
    void importFiles(const QStringList& filePaths);
    void indexLegacyFiles();
    void chooseFiles();
    void reextractSummary(const FileRecord& record);
    void updateContentState();
    void updateCounts();
    void setStatus(const QString& message);
    void updatePreview(const QModelIndex& proxyIndex);
    void setCategoryFilter(const QString& category, const QString& label);
    void setViewMode(bool compact);
    void openRecord(const FileRecord& record);

    void setupTray();
    void setAutoStart(bool enable);
    double currentNightWeight() const;
    void updateThemeForCurrentTime();
    void requestShutdown();

private slots:
    void onFileActivated(const QModelIndex& index);
    void onFileContextMenu(const QPoint& pos);

private:
    QListView* m_fileView = nullptr;
    FileListModel* m_fileModel = nullptr;
    QSortFilterProxyModel* m_proxyModel = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_engineStatusLabel = nullptr;
    QLabel* m_fileCountLabel = nullptr;
    QLabel* m_listTitleLabel = nullptr;
    QLabel* m_resultCountLabel = nullptr;
    QLabel* m_emptyTitleLabel = nullptr;
    QLabel* m_emptyDescriptionLabel = nullptr;
    QLabel* m_detailNameLabel = nullptr;
    QLabel* m_detailPathLabel = nullptr;
    QLabel* m_detailCategoryLabel = nullptr;
    QLabel* m_detailSummaryLabel = nullptr;
    QLabel* m_detailExtensionLabel = nullptr;
    QLabel* m_detailMetaLabel = nullptr;
    QLabel* m_themeIndicatorLabel = nullptr;
    QPushButton* m_openSelectedButton = nullptr;
    QToolButton* m_gridModeButton = nullptr;
    QToolButton* m_listModeButton = nullptr;
    QStackedWidget* m_contentStack = nullptr;
    QHash<QString, QPushButton*> m_navigationButtons;

    QTimer* m_searchTimer = nullptr;
    QTimer* m_themeTimer = nullptr;
    QTimer* m_shutdownTimer = nullptr;
    QThreadPool m_searchPool;
    QThreadPool m_importPool;
    std::atomic<quint64> m_searchGeneration{0};

    QSystemTrayIcon* m_trayIcon = nullptr;
    QString m_dataDir;
    QString m_activeCategory;
    QString m_activeCategoryLabel = QStringLiteral("全部文件");
    bool m_isQuitting = false;
    bool m_databaseReady = false;
    std::atomic_bool m_shutdownRequested{false};
};
