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
    
    // 【新增】本地模糊搜索
    QList<FileRecord> searchFiles(const QString& keyword);

    bool addSearchLog(const QString& query, const QString& resultFile);
    QStringList getRecentSearches(int limit = 3);
    
private:
    DatabaseManager() = default;
    QSqlDatabase db;
};