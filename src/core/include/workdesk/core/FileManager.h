#pragma once

#include <QString>

class FileManager {
public:
    static QString classifyAndMove(const QString& filePath, const QString& targetDir);
    static QString getCategory(const QString& filePath);
    static QString extractSummary(const QString& filePath);
};