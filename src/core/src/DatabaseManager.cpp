#include "workdesk/core/DatabaseManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QVariant>
#include <QStringList>

DatabaseManager& DatabaseManager::instance() {
    static DatabaseManager mgr;
    return mgr;
}

bool DatabaseManager::init(const QString& dbPath) {
    db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        qDebug() << "数据库打开失败:" << db.lastError().text();
        return false;
    }
    QSqlQuery query;
    query.exec("CREATE TABLE IF NOT EXISTS files ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "fileName TEXT,"
               "filePath TEXT UNIQUE,"
               "summary TEXT,"
               "category TEXT)");
    return query.exec("CREATE TABLE IF NOT EXISTS search_logs ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "query TEXT,"
                      "resultFile TEXT,"
                      "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP)");
}

bool DatabaseManager::addRecord(const FileRecord& record) {
    QSqlQuery query;
    query.prepare("INSERT OR REPLACE INTO files (fileName, filePath, summary, category) "
                  "VALUES (:name, :path, :summary, :category)");
    query.bindValue(":name", record.fileName);
    query.bindValue(":path", record.filePath);
    query.bindValue(":summary", record.summary);
    query.bindValue(":category", record.category);
    return query.exec();
}

QList<FileRecord> DatabaseManager::getAllRecords() {
    QList<FileRecord> list;
    QSqlQuery query("SELECT id, fileName, filePath, summary, category FROM files ORDER BY id DESC");
    while (query.next()) {
        FileRecord r;
        r.id = query.value(0).toInt();
        r.fileName = query.value(1).toString();
        r.filePath = query.value(2).toString();
        r.summary = query.value(3).toString();
        r.category = query.value(4).toString();
        list.append(r);
    }
    return list;
}

// 【新增】根据路径删除记录
bool DatabaseManager::deleteRecord(const QString& filePath) {
    QSqlQuery query;
    query.prepare("DELETE FROM files WHERE filePath = :path");
    query.bindValue(":path", filePath);
    return query.exec();
}

bool DatabaseManager::addSearchLog(const QString& query, const QString& resultFile) {
    QSqlQuery sql;
    sql.prepare("INSERT INTO search_logs (query, resultFile) VALUES (:query, :result)");
    sql.bindValue(":query", query);
    sql.bindValue(":result", resultFile);
    return sql.exec();
}

QStringList DatabaseManager::getRecentSearches(int limit) {
    QStringList list;
    QSqlQuery query;
    query.prepare("SELECT query, resultFile FROM search_logs ORDER BY id DESC LIMIT :limit");
    query.bindValue(":limit", limit);
    query.exec();
    while (query.next()) {
        list.append("用户搜过：“" + query.value(0).toString() + "”，找到了：" + query.value(1).toString());
    }
    return list;
}

// 找到文件末尾，加上这段代码
QList<FileRecord> DatabaseManager::searchFiles(const QString& keyword) {
    QList<FileRecord> list;
    QSqlQuery query;
    // 使用 LIKE 匹配文件名或摘要，前后加 % 代表模糊匹配
    query.prepare("SELECT id, fileName, filePath, summary, category FROM files "
                  "WHERE fileName LIKE :kw OR summary LIKE :kw ORDER BY id DESC");
    query.bindValue(":kw", "%" + keyword + "%");
    query.exec();
    while (query.next()) {
        FileRecord r;
        r.id = query.value(0).toInt();
        r.fileName = query.value(1).toString();
        r.filePath = query.value(2).toString();
        r.summary = query.value(3).toString();
        r.category = query.value(4).toString();
        list.append(r);
    }
    return list;
}