#pragma once
#include <QSqlDatabase>
#include <QString>
#include <QList>
#include "FileRecord.h"

class DatabaseManager {
public:
    static DatabaseManager& instance();
    bool init(const QString& dbPath);
    bool addRecord(const FileRecord& record);
    QList<FileRecord> getAllRecords();
    bool deleteRecord(const QString& filePath);
    
    // 核心升级：全文/多关键词搜索
    QList<FileRecord> searchFiles(const QString& keyword);

    bool addSearchLog(const QString& query, const QString& resultFile);
    QStringList getRecentSearches(int limit = 3);
    
private:
    DatabaseManager() = default;
    QSqlDatabase db;
    bool hasFts5 = false; // 记录是否成功开启了 FTS5 引擎
};