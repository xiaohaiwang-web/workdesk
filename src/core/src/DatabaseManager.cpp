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
    // 1. 创建主表
    query.exec("CREATE TABLE IF NOT EXISTS files ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "fileName TEXT,"
               "filePath TEXT UNIQUE,"
               "summary TEXT,"
               "category TEXT)");

    // 2. 搜索日志
    query.exec("CREATE TABLE IF NOT EXISTS search_logs ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "query TEXT,"
               "resultFile TEXT,"
               "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP)");

    // 3. 尝试创建 FTS5 虚拟表
    bool ftsCreated = query.exec("CREATE VIRTUAL TABLE IF NOT EXISTS files_fts USING fts5("
                                 "fileName, summary, content='files', content_rowid='id');");
    
    if (ftsCreated) {
        hasFts5 = true;
        // 建立自动同步触发器
        query.exec("CREATE TRIGGER IF NOT EXISTS files_ai AFTER INSERT ON files BEGIN "
                   "INSERT INTO files_fts(rowid, fileName, summary) VALUES (new.id, new.fileName, new.summary); "
                   "END;");
        query.exec("CREATE TRIGGER IF NOT EXISTS files_ad AFTER DELETE ON files BEGIN "
                   "INSERT INTO files_fts(files_fts, rowid, fileName, summary) VALUES('delete', old.id, old.fileName, old.summary); "
                   "END;");
        query.exec("CREATE TRIGGER IF NOT EXISTS files_au AFTER UPDATE ON files BEGIN "
                   "INSERT INTO files_fts(files_fts, rowid, fileName, summary) VALUES('delete', old.id, old.fileName, old.summary); "
                   "INSERT INTO files_fts(rowid, fileName, summary) VALUES (new.id, new.fileName, new.summary); "
                   "END;");
        
        // 【核心修复】全量重建索引，把旧的没进 FTS 的数据拉进来！
        query.exec("INSERT INTO files_fts(files_fts) VALUES('rebuild');");
        
        qDebug() << "✅ FTS5 全文索引引擎已开启，并已同步旧数据！";
    } else {
        hasFts5 = false;
        qDebug() << "⚠️ 当前 Qt 的 SQLite 未开启 FTS5，自动降级为普通模糊搜索。";
    }

    return true;
}

// ... 剩下的 addRecord, getAllRecords 等保持原样，不需要动 ...

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

bool DatabaseManager::deleteRecord(const QString& filePath) {
    QSqlQuery query;
    query.prepare("DELETE FROM files WHERE filePath = :path");
    query.bindValue(":path", filePath);
    return query.exec();
}

QList<FileRecord> DatabaseManager::searchFiles(const QString& keyword) {
    QList<FileRecord> list;
    if (keyword.trimmed().isEmpty()) return list;

    QSqlQuery query;
    // 支持多关键词（按空格拆分）
    QStringList words = keyword.split(" ", Qt::SkipEmptyParts);

    if (hasFts5 && !words.isEmpty()) {
        // 【极速模式】使用 FTS5 引擎
        QString ftsQuery;
        for (int i = 0; i < words.size(); ++i) {
            // 用 * 实现前缀匹配（例如搜 "数" 能匹配 "数据库"）
            ftsQuery += words[i] + "*";
            if (i != words.size() - 1) ftsQuery += " AND "; // 多个词必须同时包含
        }
        
        query.prepare("SELECT f.id, f.fileName, f.filePath, f.summary, f.category "
                      "FROM files f JOIN files_fts fts ON f.id = fts.rowid "
                      "WHERE files_fts MATCH :kw ORDER BY rank");
        query.bindValue(":kw", ftsQuery);
    } else {
        // 【降级模式】普通的 LIKE 搜索
        QString sql = "SELECT id, fileName, filePath, summary, category FROM files WHERE ";
        QStringList conditions;
        for (int i = 0; i < words.size(); ++i) {
            conditions << "fileName LIKE :kw" + QString::number(i) + " OR summary LIKE :kw" + QString::number(i);
        }
        sql += conditions.join(" OR ") + " ORDER BY id DESC";
        query.prepare(sql);
        for (int i = 0; i < words.size(); ++i) {
            query.bindValue(":kw" + QString::number(i), "%" + words[i] + "%");
        }
    }

    if (!query.exec()) {
        qDebug() << "搜索失败:" << query.lastError().text();
    }

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