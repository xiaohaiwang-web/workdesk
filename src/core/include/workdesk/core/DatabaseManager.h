#pragma once

#include <QHash>
#include <QList>
#include <QMutex>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

#include "FileRecord.h"

struct SearchPage {
    QList<FileRecord> records;
    qint64 totalCount = 0;
    int limit = 50;
    int offset = 0;
    double elapsedMs = 0.0;
    bool usedTrigram = false;
    bool shortQueryFullScan = false;
    QString error;
};

class DatabaseManager {
public:
    static DatabaseManager& instance();

    bool init(const QString& dbPath);
    bool addRecord(const FileRecord& record);
    int addRecords(const QList<FileRecord>& records);
    QList<FileRecord> getAllRecords();
    bool deleteRecord(const QString& filePath);

    // Main search API: bounded page, optional category filter, total count and elapsed time.
    SearchPage searchFilesPage(const QString& keyword, int limit = 50, int offset = 0,
                               const QString& category = QString());
    QList<FileRecord> getFilesNeedingContentIndex(int limit = 16);

    // Builds bounded AI-context chunks on demand from the single stored full-text copy.
    // No duplicated chunk table is maintained in SQLite.
    QList<FileChunk> getContentChunks(qint64 fileId, int limit = 32, int offset = 0);

    bool addSearchLog(const QString& query, const QString& resultFile);
    QStringList getRecentSearches(int limit = 3);

    bool supportsTrigramSearch() const { return m_hasTrigramFts; }

private:
    DatabaseManager() = default;
    QSqlDatabase connectionForCurrentThread() const;
    bool ensureOpen(QSqlDatabase& connection) const;

    mutable QMutex m_mutex;
    mutable QHash<QString, QSqlDatabase> m_connections;
    QString m_databasePath;
    bool m_hasFts5 = false;
    bool m_hasTrigramFts = false;
};
