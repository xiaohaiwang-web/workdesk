#include "workdesk/core/DatabaseManager.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QPair>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QVariant>

namespace {
constexpr int kMaxPageSize = 200;
constexpr int kChunkChars = 1400;
constexpr int kChunkOverlap = 160;

QString threadConnectionKey()
{
    return QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16);
}

bool tableExists(QSqlDatabase& db, const QString& tableName)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name=:name"));
    query.bindValue(QStringLiteral(":name"), tableName);
    return query.exec() && query.next();
}

bool columnExists(QSqlDatabase& db, const QString& tableName, const QString& columnName)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(tableName)))
        return false;
    while (query.next()) {
        if (query.value(1).toString().compare(columnName, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

bool execSql(QSqlDatabase& db, const QString& sql, const char* description)
{
    QSqlQuery query(db);
    if (query.exec(sql))
        return true;
    qWarning() << description << query.lastError().text() << sql;
    return false;
}

bool ensureColumn(QSqlDatabase& db, const QString& name, const QString& definition)
{
    if (columnExists(db, QStringLiteral("files"), name))
        return true;
    return execSql(db, QStringLiteral("ALTER TABLE files ADD COLUMN %1 %2").arg(name, definition),
                   "Could not add database column:");
}

QString escapeLike(QString value)
{
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    value.replace(QLatin1Char('%'), QStringLiteral("\\%"));
    value.replace(QLatin1Char('_'), QStringLiteral("\\_"));
    return value;
}

int unicodeLength(const QString& value)
{
    return static_cast<int>(value.toUcs4().size());
}

QString buildTrigramQuery(const QStringList& terms)
{
    QStringList expressions;
    expressions.reserve(terms.size());
    for (QString term : terms) {
        term.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        expressions.append(QStringLiteral("\"%1\"").arg(term));
    }
    return expressions.join(QStringLiteral(" AND "));
}

QStringList splitSearchTerms(const QString& keyword)
{
    return keyword.normalized(QString::NormalizationForm_C)
        .split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}

QStringList makeShortTerms(const QStringList& terms)
{
    QStringList result;
    for (const QString& term : terms) {
        if (unicodeLength(term) < 3)
            result.append(term);
    }
    return result;
}

QStringList makeLongTerms(const QStringList& terms)
{
    QStringList result;
    for (const QString& term : terms) {
        if (unicodeLength(term) >= 3)
            result.append(term);
    }
    return result;
}

QString makeShortFilter(const QStringList& shortTerms, bool includeContent)
{
    QStringList conditions;
    for (int i = 0; i < shortTerms.size(); ++i) {
        const QString parameter = QStringLiteral(":short%1").arg(i);
        QString condition = QStringLiteral(
            "(f.fileName LIKE %1 ESCAPE '\\' OR f.summary LIKE %1 ESCAPE '\\'")
            .arg(parameter);
        if (includeContent) {
            condition += QStringLiteral(" OR f.full_text LIKE %1 ESCAPE '\\'").arg(parameter);
        }
        condition += QLatin1Char(')');
        conditions.append(condition);
    }
    return conditions.join(QStringLiteral(" AND "));
}

QList<FileChunk> makeChunkPage(const QString& text, qint64 fileId, int limit, int offset)
{
    QList<FileChunk> chunks;
    if (text.isEmpty())
        return chunks;

    const int length = text.size();
    const int stride = kChunkChars - kChunkOverlap;
    int chunkIndex = qMax(0, offset);
    const int maxChunks = qBound(1, limit, 256);
    chunks.reserve(maxChunks);

    while (chunks.size() < maxChunks) {
        if (chunkIndex > length / stride)
            break;
        int start = chunkIndex * stride;
        if (start >= length)
            break;
        if (start > 0 && start < length && text.at(start).isLowSurrogate())
            ++start;

        int end = qMin(start + kChunkChars, length);
        if (end < length && end > start
            && text.at(end - 1).isHighSurrogate() && text.at(end).isLowSurrogate()) {
            --end;
        }
        if (end <= start)
            break;

        const QString content = text.mid(start, end - start).trimmed();
        if (!content.isEmpty()) {
            FileChunk chunk;
            chunk.id = chunkIndex;
            chunk.fileId = fileId;
            chunk.chunkIndex = chunkIndex;
            chunk.content = content;
            chunks.append(chunk);
        }
        ++chunkIndex;
        if (end >= length)
            break;
    }
    return chunks;
}

void bindShortTerms(QSqlQuery& query, const QStringList& shortTerms)
{
    for (int i = 0; i < shortTerms.size(); ++i) {
        query.bindValue(QStringLiteral(":short%1").arg(i),
                        QStringLiteral("%") + escapeLike(shortTerms.at(i)) + QStringLiteral("%"));
    }
}

void bindFallbackTerms(QSqlQuery& query, const QStringList& terms)
{
    for (int i = 0; i < terms.size(); ++i) {
        query.bindValue(QStringLiteral(":fallback%1").arg(i),
                        QStringLiteral("%") + escapeLike(terms.at(i)) + QStringLiteral("%"));
    }
}
}

DatabaseManager& DatabaseManager::instance()
{
    static DatabaseManager manager;
    return manager;
}

QSqlDatabase DatabaseManager::connectionForCurrentThread() const
{
    const QString key = threadConnectionKey();
    QSqlDatabase connection;
    {
        QMutexLocker locker(&m_mutex);
        const auto it = m_connections.constFind(key);
        if (it != m_connections.constEnd()) {
            connection = it.value();
        } else {
            if (m_databasePath.isEmpty()) {
                qWarning() << "Database path has not been initialized";
                return {};
            }
            const QString connectionName = QStringLiteral("workdesk_%1").arg(key);
            connection = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            connection.setDatabaseName(m_databasePath);
            connection.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
            m_connections.insert(key, connection);
        }
    }

    if (!connection.isOpen()) {
        if (!connection.open()) {
            qWarning() << "SQLite connection open failed:" << connection.lastError().text();
            return {};
        }
        QSqlQuery pragma(connection);
        pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));
        pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
        pragma.exec(QStringLiteral("PRAGMA synchronous = NORMAL"));
        pragma.exec(QStringLiteral("PRAGMA temp_store = MEMORY"));
        pragma.exec(QStringLiteral("PRAGMA cache_size = -8192")); // ~8 MiB page cache per connection.
    }
    return connection;
}

bool DatabaseManager::ensureOpen(QSqlDatabase& connection) const
{
    if (!connection.isValid())
        return false;
    if (connection.isOpen())
        return true;
    if (!connection.open()) {
        qWarning() << "SQLite connection open failed:" << connection.lastError().text();
        return false;
    }

    QSqlQuery pragma(connection);
    pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    pragma.exec(QStringLiteral("PRAGMA synchronous = NORMAL"));
    pragma.exec(QStringLiteral("PRAGMA temp_store = MEMORY"));
    pragma.exec(QStringLiteral("PRAGMA cache_size = -8192"));
    return true;
}

bool DatabaseManager::init(const QString& dbPath)
{
    {
        QMutexLocker locker(&m_mutex);
        m_databasePath = dbPath;
    }

    QSqlDatabase connection = connectionForCurrentThread();
    if (!ensureOpen(connection))
        return false;

    QSqlQuery query(connection);
    if (!query.exec(QStringLiteral("PRAGMA journal_mode = WAL")))
        qWarning() << "Could not enable SQLite WAL:" << query.lastError().text();
    query.exec(QStringLiteral("PRAGMA synchronous = NORMAL"));

    const QStringList schemaStatements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS files ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "fileName TEXT NOT NULL,"
                       "filePath TEXT NOT NULL UNIQUE,"
                       "summary TEXT,"
                       "category TEXT)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS search_logs ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "query TEXT, resultFile TEXT,"
                       "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS search_index_state ("
                       "key TEXT PRIMARY KEY, value TEXT NOT NULL)")
    };
    for (const QString& statement : schemaStatements) {
        if (!execSql(connection, statement, "Database schema migration failed:"))
            return false;
    }

    // Additive migration: the v1 file table and all existing file records are preserved.
    const QList<QPair<QString, QString>> columns = {
        {QStringLiteral("full_text"), QStringLiteral("TEXT NOT NULL DEFAULT ''")},
        {QStringLiteral("file_size"), QStringLiteral("INTEGER NOT NULL DEFAULT 0")},
        {QStringLiteral("modified_at"), QStringLiteral("TEXT")},
        {QStringLiteral("content_hash"), QStringLiteral("TEXT")},
        {QStringLiteral("extracted_chars"), QStringLiteral("INTEGER NOT NULL DEFAULT 0")},
        {QStringLiteral("content_truncated"), QStringLiteral("INTEGER NOT NULL DEFAULT 0")},
        {QStringLiteral("extract_status"), QStringLiteral("TEXT NOT NULL DEFAULT 'pending'")},
        {QStringLiteral("indexed_at"), QStringLiteral("TEXT")}
    };
    for (const auto& column : columns) {
        if (!ensureColumn(connection, column.first, column.second))
            return false;
    }

    if (!execSql(connection,
                 QStringLiteral("CREATE INDEX IF NOT EXISTS idx_files_category_id ON files(category, id DESC)"),
                 "Could not create file category index:")) {
        return false;
    }

    // Preserve the original FTS5 table for compatibility with older app versions.
    const bool legacyFtsExisted = tableExists(connection, QStringLiteral("files_fts"));
    if (query.exec(QStringLiteral(
            "CREATE VIRTUAL TABLE IF NOT EXISTS files_fts USING fts5("
            "fileName, summary, content='files', content_rowid='id')"))) {
        m_hasFts5 = true;
        // Older versions installed an AFTER UPDATE trigger for every column. Narrow it so
        // full-text-only updates do not rewrite the legacy metadata index unnecessarily.
        if (!execSql(connection, QStringLiteral("DROP TRIGGER IF EXISTS files_au"),
                     "Could not replace legacy FTS update trigger:")) {
            m_hasFts5 = false;
        }
        const QStringList legacyTriggers = {
            QStringLiteral("CREATE TRIGGER IF NOT EXISTS files_ai AFTER INSERT ON files BEGIN "
                           "INSERT INTO files_fts(rowid, fileName, summary) "
                           "VALUES(new.id, new.fileName, new.summary); END"),
            QStringLiteral("CREATE TRIGGER IF NOT EXISTS files_ad AFTER DELETE ON files BEGIN "
                           "INSERT INTO files_fts(files_fts, rowid, fileName, summary) "
                           "VALUES('delete', old.id, old.fileName, old.summary); END"),
            QStringLiteral("CREATE TRIGGER IF NOT EXISTS files_au AFTER UPDATE OF fileName,summary ON files "
                           "WHEN old.fileName IS NOT new.fileName OR old.summary IS NOT new.summary BEGIN "
                           "INSERT INTO files_fts(files_fts, rowid, fileName, summary) "
                           "VALUES('delete', old.id, old.fileName, old.summary); "
                           "INSERT INTO files_fts(rowid, fileName, summary) "
                           "VALUES(new.id, new.fileName, new.summary); END")
        };
        for (const QString& statement : legacyTriggers) {
            if (!execSql(connection, statement, "Could not create legacy FTS trigger:")) {
                m_hasFts5 = false;
                break;
            }
        }
        if (m_hasFts5 && !legacyFtsExisted) {
            if (!execSql(connection, QStringLiteral("INSERT INTO files_fts(files_fts) VALUES('rebuild')"),
                         "Legacy FTS initial rebuild failed:")) {
                m_hasFts5 = false;
            }
        }
    } else {
        m_hasFts5 = false;
        qWarning() << "Legacy FTS5 table unavailable:" << query.lastError().text();
    }

    // One FTS row per file: filename, summary and full text share the same searchable document.
    // Remember whether the virtual table existed before CREATE IF NOT EXISTS: a stale state
    // marker must not make a newly recreated (empty) external-content index look ready.
    const bool searchIndexExisted = tableExists(connection, QStringLiteral("files_search_fts"));
    const bool searchIndexCreated = query.exec(QStringLiteral(
        "CREATE VIRTUAL TABLE IF NOT EXISTS files_search_fts USING fts5("
        "fileName, summary, full_text, content='files', content_rowid='id', tokenize='trigram')"));
    m_hasTrigramFts = searchIndexCreated;

    if (m_hasTrigramFts) {
        const QStringList searchTriggers = {
            QStringLiteral("CREATE TRIGGER IF NOT EXISTS files_search_ai AFTER INSERT ON files BEGIN "
                           "INSERT INTO files_search_fts(rowid,fileName,summary,full_text) "
                           "VALUES(new.id,new.fileName,new.summary,new.full_text); END"),
            QStringLiteral("CREATE TRIGGER IF NOT EXISTS files_search_ad AFTER DELETE ON files BEGIN "
                           "INSERT INTO files_search_fts(files_search_fts,rowid,fileName,summary,full_text) "
                           "VALUES('delete',old.id,old.fileName,old.summary,old.full_text); END"),
            QStringLiteral("CREATE TRIGGER IF NOT EXISTS files_search_au "
                           "AFTER UPDATE OF fileName,summary,full_text ON files "
                           "WHEN old.fileName IS NOT new.fileName OR old.summary IS NOT new.summary "
                           "OR old.full_text IS NOT new.full_text BEGIN "
                           "INSERT INTO files_search_fts(files_search_fts,rowid,fileName,summary,full_text) "
                           "VALUES('delete',old.id,old.fileName,old.summary,old.full_text); "
                           "INSERT INTO files_search_fts(rowid,fileName,summary,full_text) "
                           "VALUES(new.id,new.fileName,new.summary,new.full_text); END")
        };
        for (const QString& statement : searchTriggers) {
            if (!execSql(connection, statement, "Could not create unified search trigger:")) {
                m_hasTrigramFts = false;
                break;
            }
        }

        if (m_hasTrigramFts) {
            QSqlQuery stateQuery(connection);
            stateQuery.prepare(QStringLiteral("SELECT value FROM search_index_state WHERE key=:key"));
            stateQuery.bindValue(QStringLiteral(":key"), QStringLiteral("files_search_trigram_v1"));
            const bool indexReady = stateQuery.exec() && stateQuery.next()
                && stateQuery.value(0).toString() == QStringLiteral("1");
            stateQuery.finish();

            if (!searchIndexExisted || !indexReady) {
                if (!execSql(connection, QStringLiteral("INSERT INTO files_search_fts(files_search_fts) VALUES('rebuild')"),
                             "Unified trigram index rebuild failed:")) {
                    m_hasTrigramFts = false;
                } else {
                    QSqlQuery mark(connection);
                    mark.prepare(QStringLiteral(
                        "INSERT OR REPLACE INTO search_index_state(key,value) VALUES(:key,'1')"));
                    mark.bindValue(QStringLiteral(":key"), QStringLiteral("files_search_trigram_v1"));
                    if (!mark.exec())
                        qWarning() << "Could not mark unified search index as ready:" << mark.lastError().text();
                }
            }
        }
    } else {
        qWarning() << "FTS5 trigram tokenizer unavailable; using the compatibility LIKE search path:"
                   << query.lastError().text();
    }

    execSql(connection, QStringLiteral("PRAGMA user_version = 2"), "Could not update schema version:");
    qInfo() << "WorkDesk database ready. FTS5:" << m_hasFts5
            << "unified trigram:" << m_hasTrigramFts;
    return true;
}

bool DatabaseManager::addRecord(const FileRecord& record)
{
    return addRecords(QList<FileRecord>{record}) == 1;
}

int DatabaseManager::addRecords(const QList<FileRecord>& records)
{
    if (records.isEmpty())
        return 0;

    QSqlDatabase connection = connectionForCurrentThread();
    if (!ensureOpen(connection))
        return 0;
    if (!connection.transaction()) {
        qWarning() << "Could not start file batch transaction:" << connection.lastError().text();
        return 0;
    }

    QSqlQuery metadataQuery(connection);
    metadataQuery.prepare(QStringLiteral(
        "INSERT INTO files(fileName,filePath,summary,category) "
        "VALUES(:name,:path,:summary,:category) "
        "ON CONFLICT(filePath) DO UPDATE SET "
        "fileName=excluded.fileName,summary=excluded.summary,category=excluded.category "
        "WHERE files.fileName IS NOT excluded.fileName "
        "OR files.summary IS NOT excluded.summary OR files.category IS NOT excluded.category"));

    QSqlQuery contentQuery(connection);
    contentQuery.prepare(QStringLiteral(
        "INSERT INTO files(fileName,filePath,summary,category,file_size,modified_at,content_hash,"
        "extracted_chars,content_truncated,extract_status,indexed_at,full_text) "
        "VALUES(:name,:path,:summary,:category,:size,:modified,:hash,:chars,:truncated,:status,"
        "CURRENT_TIMESTAMP,:full_text) "
        "ON CONFLICT(filePath) DO UPDATE SET "
        "fileName=excluded.fileName,summary=excluded.summary,category=excluded.category,"
        "file_size=excluded.file_size,modified_at=excluded.modified_at,content_hash=excluded.content_hash,"
        "extracted_chars=excluded.extracted_chars,content_truncated=excluded.content_truncated,"
        "extract_status=excluded.extract_status,indexed_at=CURRENT_TIMESTAMP,full_text=excluded.full_text "
        "WHERE files.fileName IS NOT excluded.fileName OR files.summary IS NOT excluded.summary "
        "OR files.category IS NOT excluded.category OR files.file_size IS NOT excluded.file_size "
        "OR files.modified_at IS NOT excluded.modified_at OR files.content_hash IS NOT excluded.content_hash "
        "OR files.extract_status IS NOT excluded.extract_status OR files.full_text IS NOT excluded.full_text"));

    bool batchOk = true;
    for (const FileRecord& record : records) {
        QSqlQuery& query = record.contentExtracted ? contentQuery : metadataQuery;
        query.bindValue(QStringLiteral(":name"), record.fileName);
        query.bindValue(QStringLiteral(":path"), record.filePath);
        query.bindValue(QStringLiteral(":summary"), record.summary);
        query.bindValue(QStringLiteral(":category"), record.category);

        if (record.contentExtracted) {
            QString contentHash = record.contentHash;
            if (contentHash.isEmpty()) {
                contentHash = QString::fromLatin1(
                    QCryptographicHash::hash(record.fullText.toUtf8(), QCryptographicHash::Sha256).toHex());
            }
            const QString extractStatus = record.fullText.isEmpty()
                ? QStringLiteral("no_text") : (record.contentTruncated ? QStringLiteral("truncated")
                                                                        : QStringLiteral("ok"));
            query.bindValue(QStringLiteral(":size"), record.fileSize);
            query.bindValue(QStringLiteral(":modified"), record.modifiedAt);
            query.bindValue(QStringLiteral(":hash"), contentHash);
            query.bindValue(QStringLiteral(":chars"), record.fullText.size());
            query.bindValue(QStringLiteral(":truncated"), record.contentTruncated ? 1 : 0);
            query.bindValue(QStringLiteral(":status"), extractStatus);
            query.bindValue(QStringLiteral(":full_text"), record.fullText);
        }

        if (!query.exec()) {
            qWarning() << "Could not save file/index record:" << query.lastError().text() << record.filePath;
            batchOk = false;
            break;
        }
        query.finish();
    }

    metadataQuery.finish();
    contentQuery.finish();
    if (!batchOk) {
        connection.rollback();
        return 0;
    }

    if (!connection.commit()) {
        qWarning() << "Could not commit file batch:" << connection.lastError().text();
        connection.rollback();
        return 0;
    }
    return records.size();
}

QList<FileRecord> DatabaseManager::getAllRecords()
{
    QList<FileRecord> records;
    QSqlDatabase connection = connectionForCurrentThread();
    if (!ensureOpen(connection))
        return records;

    QSqlQuery query(connection);
    if (!query.exec(QStringLiteral(
            "SELECT id,fileName,filePath,summary,category FROM files ORDER BY id DESC"))) {
        qWarning() << "Could not load file records:" << query.lastError().text();
        return records;
    }

    while (query.next()) {
        FileRecord record{};
        record.id = query.value(0).toLongLong();
        record.fileName = query.value(1).toString();
        record.filePath = query.value(2).toString();
        record.summary = query.value(3).toString();
        record.category = query.value(4).toString();
        records.append(record);
    }
    return records;
}

bool DatabaseManager::deleteRecord(const QString& filePath)
{
    QSqlDatabase connection = connectionForCurrentThread();
    if (!ensureOpen(connection))
        return false;

    QSqlQuery query(connection);
    query.prepare(QStringLiteral("DELETE FROM files WHERE filePath=:path"));
    query.bindValue(QStringLiteral(":path"), filePath);
    const bool success = query.exec();
    if (!success)
        qWarning() << "Could not delete file record:" << query.lastError().text();
    return success;
}

SearchPage DatabaseManager::searchFilesPage(const QString& keyword, int limit, int offset,
                                             const QString& category)
{
    QElapsedTimer timer;
    timer.start();

    SearchPage page;
    page.limit = qBound(1, limit, kMaxPageSize);
    page.offset = qMax(0, offset);

    QSqlDatabase connection = connectionForCurrentThread();
    if (!ensureOpen(connection)) {
        page.error = QStringLiteral("数据库连接不可用");
        return page;
    }

    const QString normalizedKeyword = keyword.trimmed().normalized(QString::NormalizationForm_C);
    const QStringList allTerms = splitSearchTerms(normalizedKeyword);
    const QStringList longTerms = makeLongTerms(allTerms);
    const QStringList shortTerms = makeShortTerms(allTerms);
    const bool trigramQuery = m_hasTrigramFts && !longTerms.isEmpty();
    page.usedTrigram = trigramQuery;
    page.shortQueryFullScan = !normalizedKeyword.isEmpty() && longTerms.isEmpty();

    QString whereClause;
    QString fromClause;
    QString orderClause;
    QString rankingFilter;
    if (normalizedKeyword.isEmpty()) {
        fromClause = QStringLiteral("FROM files f");
        whereClause = category.isEmpty() ? QStringLiteral("1=1") : QStringLiteral("f.category=:category");
        orderClause = QStringLiteral("ORDER BY f.id DESC");
    } else if (trigramQuery) {
        fromClause = QStringLiteral("FROM files_search_fts JOIN files f ON f.id=files_search_fts.rowid");
        QStringList filters;
        filters.append(QStringLiteral("files_search_fts MATCH :fts_query"));
        const QString shortFilter = makeShortFilter(shortTerms, true);
        if (!shortFilter.isEmpty())
            filters.append(shortFilter);
        if (!category.isEmpty())
            filters.append(QStringLiteral("f.category=:category"));
        whereClause = filters.join(QStringLiteral(" AND "));
        // Configure the FTS5 rank column once per query. ORDER BY rank is preferable
        // to spelling bm25() directly when LIMIT is present.
        rankingFilter = QStringLiteral("files_search_fts.rank MATCH 'bm25(8.0,1.0,0.5)'");
        orderClause = QStringLiteral("ORDER BY files_search_fts.rank ASC,f.id DESC");
    } else {
        // Trigram FTS cannot index 1-2 character strings. For short-only queries, or
        // when trigram support is unavailable, LIKE checks the full body for correctness.
        // This is intentionally a slower compatibility path and runs on a worker thread.
        QStringList filters;
        for (int i = 0; i < allTerms.size(); ++i) {
            const QString parameter = QStringLiteral(":fallback%1").arg(i);
            QString condition = QStringLiteral(
                "(f.fileName LIKE %1 ESCAPE '\\' OR f.summary LIKE %1 ESCAPE '\\'")
                .arg(parameter);
            condition += QStringLiteral(" OR f.full_text LIKE %1 ESCAPE '\\'").arg(parameter);
            condition += QLatin1Char(')');
            filters.append(condition);
        }
        if (!category.isEmpty())
            filters.append(QStringLiteral("f.category=:category"));
        whereClause = filters.isEmpty() ? QStringLiteral("1=1")
                                        : filters.join(QStringLiteral(" AND "));
        fromClause = QStringLiteral("FROM files f");
        orderClause = QStringLiteral("ORDER BY f.id DESC");
    }

    // Keep exact result counts for the UI. The FTS index counts matching file rows, not chunks.
    QSqlQuery countQuery(connection);
    const QString countSql = QStringLiteral("SELECT COUNT(*) ") + fromClause
        + QStringLiteral(" WHERE ") + whereClause;
    countQuery.prepare(countSql);
    if (trigramQuery) {
        countQuery.bindValue(QStringLiteral(":fts_query"), buildTrigramQuery(longTerms));
        bindShortTerms(countQuery, shortTerms);
        if (!category.isEmpty())
            countQuery.bindValue(QStringLiteral(":category"), category);
    } else if (!normalizedKeyword.isEmpty()) {
        bindFallbackTerms(countQuery, allTerms);
        if (!category.isEmpty())
            countQuery.bindValue(QStringLiteral(":category"), category);
    } else if (!category.isEmpty()) {
        countQuery.bindValue(QStringLiteral(":category"), category);
    }

    if (!countQuery.exec()) {
        page.error = QStringLiteral("搜索统计失败：%1").arg(countQuery.lastError().text());
        page.elapsedMs = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
        return page;
    }
    if (countQuery.next())
        page.totalCount = countQuery.value(0).toLongLong();
    countQuery.finish();

    QString selectSql;
    if (trigramQuery) {
        selectSql = QStringLiteral(
            "SELECT f.id,f.fileName,f.filePath,f.summary,f.category,"
            "snippet(files_search_fts,-1,'[',']','…',18) AS matchedText,"
            "files_search_fts.rank AS score ")
            + fromClause + QStringLiteral(" WHERE ") + whereClause
            + QStringLiteral(" AND ") + rankingFilter + QLatin1Char(' ')
            + orderClause + QStringLiteral(" LIMIT :limit OFFSET :offset");
    } else {
        selectSql = QStringLiteral(
            "SELECT f.id,f.fileName,f.filePath,f.summary,f.category,f.summary AS matchedText,0.0 AS score ")
            + fromClause + QStringLiteral(" WHERE ") + whereClause + QLatin1Char(' ')
            + orderClause + QStringLiteral(" LIMIT :limit OFFSET :offset");
    }

    QSqlQuery query(connection);
    query.prepare(selectSql);
    if (trigramQuery) {
        query.bindValue(QStringLiteral(":fts_query"), buildTrigramQuery(longTerms));
        bindShortTerms(query, shortTerms);
        if (!category.isEmpty())
            query.bindValue(QStringLiteral(":category"), category);
    } else if (!normalizedKeyword.isEmpty()) {
        bindFallbackTerms(query, allTerms);
        if (!category.isEmpty())
            query.bindValue(QStringLiteral(":category"), category);
    } else if (!category.isEmpty()) {
        query.bindValue(QStringLiteral(":category"), category);
    }
    query.bindValue(QStringLiteral(":limit"), page.limit);
    query.bindValue(QStringLiteral(":offset"), page.offset);

    if (!query.exec()) {
        page.error = QStringLiteral("搜索执行失败：%1").arg(query.lastError().text());
        page.elapsedMs = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
        return page;
    }

    while (query.next()) {
        FileRecord record{};
        record.id = query.value(0).toLongLong();
        record.fileName = query.value(1).toString();
        record.filePath = query.value(2).toString();
        record.summary = query.value(3).toString();
        record.category = query.value(4).toString();
        record.matchedText = query.value(5).toString();
        record.relevanceScore = query.value(6).toDouble();
        page.records.append(record);
    }

    page.elapsedMs = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
    return page;
}

QList<FileRecord> DatabaseManager::getFilesNeedingContentIndex(int limit)
{
    QList<FileRecord> records;
    QSqlDatabase connection = connectionForCurrentThread();
    if (!ensureOpen(connection))
        return records;

    QSqlQuery query(connection);
    query.prepare(QStringLiteral(
        "SELECT id,fileName,filePath,summary,category,file_size,modified_at "
        "FROM files WHERE content_hash IS NULL ORDER BY id ASC LIMIT :limit"));
    query.bindValue(QStringLiteral(":limit"), qBound(1, limit, 128));
    if (!query.exec()) {
        qWarning() << "Could not read legacy files awaiting full-text indexing:" << query.lastError().text();
        return records;
    }

    while (query.next()) {
        FileRecord record{};
        record.id = query.value(0).toLongLong();
        record.fileName = query.value(1).toString();
        record.filePath = query.value(2).toString();
        record.summary = query.value(3).toString();
        record.category = query.value(4).toString();
        record.fileSize = query.value(5).toLongLong();
        record.modifiedAt = query.value(6).toString();
        records.append(record);
    }
    return records;
}

QList<FileChunk> DatabaseManager::getContentChunks(qint64 fileId, int limit, int offset)
{
    QList<FileChunk> chunks;
    QSqlDatabase connection = connectionForCurrentThread();
    if (!ensureOpen(connection))
        return chunks;

    QSqlQuery query(connection);
    query.prepare(QStringLiteral("SELECT full_text FROM files WHERE id=:file_id"));
    query.bindValue(QStringLiteral(":file_id"), fileId);
    if (!query.exec() || !query.next()) {
        if (query.lastError().isValid())
            qWarning() << "Could not read indexed file text:" << query.lastError().text();
        return chunks;
    }

    const QString fullText = query.value(0).toString();
    query.finish();
    return makeChunkPage(fullText, fileId, limit, offset);
}

bool DatabaseManager::addSearchLog(const QString& queryText, const QString& resultFile)
{
    QSqlDatabase connection = connectionForCurrentThread();
    if (!ensureOpen(connection))
        return false;

    QSqlQuery query(connection);
    query.prepare(QStringLiteral("INSERT INTO search_logs(query,resultFile) VALUES(:query,:result)"));
    query.bindValue(QStringLiteral(":query"), queryText);
    query.bindValue(QStringLiteral(":result"), resultFile);
    return query.exec();
}

QStringList DatabaseManager::getRecentSearches(int limit)
{
    QStringList records;
    QSqlDatabase connection = connectionForCurrentThread();
    if (!ensureOpen(connection))
        return records;

    QSqlQuery query(connection);
    query.prepare(QStringLiteral("SELECT query,resultFile FROM search_logs ORDER BY id DESC LIMIT :limit"));
    query.bindValue(QStringLiteral(":limit"), qMax(0, limit));
    if (!query.exec())
        return records;

    while (query.next()) {
        records.append(QStringLiteral("用户搜过：“%1”，找到了：%2")
                           .arg(query.value(0).toString(), query.value(1).toString()));
    }
    return records;
}
