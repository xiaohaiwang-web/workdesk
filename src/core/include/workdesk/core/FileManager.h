#pragma once

#include <QString>
#include <atomic>

class FileManager {
public:
    static QString classifyAndMove(const QString& filePath, const QString& targetDir);
    static QString getCategory(const QString& filePath);
    static bool isImageFile(const QString& filePath);
    static bool isSupportedOfficeFile(const QString& filePath);
    static QString noTextSummary(const QString& filePath);

    // Extracts searchable text with format-specific size limits. Standalone image files
    // remain metadata-only; scanned PDF pages may use the optional local Tesseract OCR engine.
    // The cancellation token is checked between parser/OCR operations during shutdown.
    static QString extractContent(const QString& filePath, bool* truncated = nullptr,
                                  const std::atomic_bool* cancel = nullptr);
    static QString makeSummary(const QString& fullText);
    static QString extractSummary(const QString& filePath);
};
