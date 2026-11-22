#pragma once

#include <QString>
#include <QtGlobal>

// FileRecord is the lightweight file metadata passed between the UI and core.
// fullText is transient during indexing and stored once in files.full_text.
// FileChunk values are generated on demand when an AI provider needs context.
struct FileRecord {
    qint64 id = 0;
    QString fileName;
    QString filePath;
    QString summary;
    QString category;

    QString fullText;
    QString contentHash;
    QString modifiedAt;
    QString matchedText;
    qint64 fileSize = 0;
    bool contentExtracted = false;
    bool contentTruncated = false;
    double relevanceScore = 0.0;
};

struct FileChunk {
    qint64 id = 0;
    qint64 fileId = 0;
    int chunkIndex = 0;
    QString content;
};
