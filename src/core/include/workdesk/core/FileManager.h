#pragma once

#include <QString>

class FileManager {
public:
    static QString classifyAndMove(const QString& filePath, const QString& targetDir);
    static QString getCategory(const QString& filePath);

    // Extracts searchable text, capped to keep indexing memory and database growth bounded.
    // `truncated` is set when the source exceeds the extraction limit.
    static QString extractContent(const QString& filePath, bool* truncated = nullptr);
    static QString extractSummary(const QString& filePath);
};
