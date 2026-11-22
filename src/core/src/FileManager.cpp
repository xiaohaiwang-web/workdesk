#include "workdesk/core/FileManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QTemporaryDir>
#include <QDebug>

#include <algorithm>
#include <exception>
#include <string>

extern "C" {
#include <zip.h>
}

#include <pugixml.hpp>
#include <aspose/pdf/document.hpp>
#include <aspose/pdf/text_absorber.hpp>

namespace {
constexpr qint64 kMaxExtractBytes = 8 * 1024 * 1024;
constexpr qint64 kMaxDocxXmlBytes = 64 * 1024 * 1024;
constexpr qint64 kMaxPdfFileBytes = 256 * 1024 * 1024;

QString normalizedText(QString text)
{
    text.replace(QChar('\0'), QChar(' '));
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text = text.normalized(QString::NormalizationForm_C);
    text.replace(QRegularExpression(QStringLiteral("[\\t\\f\\v ]+")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral(" *\\n *")), QStringLiteral("\n"));
    text.replace(QRegularExpression(QStringLiteral("\\n{3,}")), QStringLiteral("\n\n"));
    return text.trimmed();
}

bool isPlainTextExtension(const QString& ext)
{
    static const QSet<QString> extensions = {
        QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("markdown"),
        QStringLiteral("rst"), QStringLiteral("cpp"), QStringLiteral("cc"),
        QStringLiteral("cxx"), QStringLiteral("c"), QStringLiteral("h"),
        QStringLiteral("hh"), QStringLiteral("hpp"), QStringLiteral("hxx"),
        QStringLiteral("py"), QStringLiteral("pyi"), QStringLiteral("js"),
        QStringLiteral("jsx"), QStringLiteral("mjs"), QStringLiteral("cjs"),
        QStringLiteral("ts"), QStringLiteral("tsx"), QStringLiteral("vue"),
        QStringLiteral("json"), QStringLiteral("jsonc"), QStringLiteral("xml"),
        QStringLiteral("html"), QStringLiteral("htm"), QStringLiteral("css"),
        QStringLiteral("scss"), QStringLiteral("sass"), QStringLiteral("less"),
        QStringLiteral("csv"), QStringLiteral("tsv"), QStringLiteral("log"),
        QStringLiteral("ini"), QStringLiteral("cfg"), QStringLiteral("conf"),
        QStringLiteral("yaml"), QStringLiteral("yml"), QStringLiteral("toml"),
        QStringLiteral("properties"), QStringLiteral("sql"), QStringLiteral("java"),
        QStringLiteral("kt"), QStringLiteral("kts"), QStringLiteral("cs"),
        QStringLiteral("go"), QStringLiteral("rs"), QStringLiteral("php"),
        QStringLiteral("rb"), QStringLiteral("sh"), QStringLiteral("bash"),
        QStringLiteral("zsh"), QStringLiteral("ps1"), QStringLiteral("bat"),
        QStringLiteral("cmd"), QStringLiteral("cmake"), QStringLiteral("gradle"),
        QStringLiteral("make"), QStringLiteral("tex"), QStringLiteral("bib")
    };
    return extensions.contains(ext);
}

QString powershellLiteral(QString value)
{
    value.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(value);
}

QString decodeUtf8Bounded(const QByteArray& bytes, bool* truncated)
{
    QByteArray bounded = bytes;
    const bool wasTruncated = bounded.size() > kMaxExtractBytes;
    if (wasTruncated) {
        bounded.truncate(static_cast<qsizetype>(kMaxExtractBytes));
        if (truncated)
            *truncated = true;
    }

    QString decoded;
    QStringDecoder utf8Decoder(QStringDecoder::Utf8);
    decoded = utf8Decoder.decode(bounded);
    if (utf8Decoder.hasError()) {
        if (bounded.startsWith("\xFF\xFE") || bounded.startsWith("\xFE\xFF")) {
            QStringDecoder utf16Decoder(QStringDecoder::Utf16);
            decoded = utf16Decoder.decode(bounded);
        } else if (!wasTruncated) {
            // Many legacy Windows text files are saved in the active ANSI code page.
            decoded = QString::fromLocal8Bit(bounded);
        }
    }
    return normalizedText(decoded);
}
}

QString FileManager::getCategory(const QString& filePath)
{
    const QString ext = QFileInfo(filePath).suffix().toLower();
    if (ext == QStringLiteral("pdf")) return QStringLiteral("PDF文档");
    if (ext == QStringLiteral("doc") || ext == QStringLiteral("docx")) return QStringLiteral("Word文档");
    if (ext == QStringLiteral("xls") || ext == QStringLiteral("xlsx")) return QStringLiteral("Excel表格");
    if (ext == QStringLiteral("jpg") || ext == QStringLiteral("png")
        || ext == QStringLiteral("jpeg") || ext == QStringLiteral("gif")) return QStringLiteral("图片");
    if (isPlainTextExtension(ext)) return QStringLiteral("文本代码");
    return QStringLiteral("其他");
}

QString FileManager::classifyAndMove(const QString& filePath, const QString& targetDir)
{
    const QFileInfo fi(filePath);
    const QString category = getCategory(filePath);

    QDir dir(targetDir);
    if (!dir.exists(category) && !dir.mkpath(category))
        return {};

    QString destPath = dir.filePath(category + QLatin1Char('/') + fi.fileName());
    if (QFileInfo(destPath).absoluteFilePath() == fi.absoluteFilePath())
        return destPath;

    if (QFile::exists(destPath)) {
        destPath = dir.filePath(category + QLatin1Char('/') + fi.completeBaseName()
            + QLatin1Char('_') + QString::number(QDateTime::currentMSecsSinceEpoch())
            + QLatin1Char('.') + fi.suffix());
    }

    if (QFile::rename(filePath, destPath))
        return destPath;
    if (QFile::copy(filePath, destPath)) {
        QFile::remove(filePath);
        return destPath;
    }
    return {};
}

QString FileManager::extractContent(const QString& filePath, bool* truncated)
{
    if (truncated)
        *truncated = false;

    const QFileInfo fi(filePath);
    const QString ext = fi.suffix().toLower();

    // Plain text and source files: read a bounded amount to avoid unbounded allocations.
    if (isPlainTextExtension(ext)) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "Text extraction failed to open:" << filePath << file.errorString();
            return {};
        }
        QByteArray data = file.read(kMaxExtractBytes + 1);
        if (data.size() > kMaxExtractBytes && truncated)
            *truncated = true;
        return decodeUtf8Bounded(data, truncated);
    }

    if (ext == QStringLiteral("pdf")) {
        if (fi.size() > kMaxPdfFileBytes) {
            if (truncated)
                *truncated = true;
            qWarning() << "PDF exceeds the configured extraction size limit:" << filePath;
            return {};
        }
        try {
            Aspose::Pdf::Document doc(filePath.toStdString());
            Aspose::Pdf::Text::TextAbsorber absorber;
            absorber.Visit(doc);
            std::string fullText = absorber.Text();
            if (static_cast<qint64>(fullText.size()) > kMaxExtractBytes) {
                fullText.resize(static_cast<size_t>(kMaxExtractBytes));
                if (truncated)
                    *truncated = true;
            }
            return normalizedText(QString::fromUtf8(fullText.data(),
                                                     static_cast<qsizetype>(fullText.size())));
        } catch (const std::exception& e) {
            qWarning() << "PDF extraction failed:" << e.what() << filePath;
            return {};
        }
    }

    if (ext == QStringLiteral("doc")) {
        QTemporaryDir tempDir;
        if (!tempDir.isValid())
            return {};

        const QString tempDocx = QDir(tempDir.path()).filePath(QStringLiteral("converted.docx"));
        const QString winPath = QDir::toNativeSeparators(filePath);
        const QString winTemp = QDir::toNativeSeparators(tempDocx);
        const QString script = QStringLiteral(
            "$ErrorActionPreference='Stop'; "
            "$word=$null; $doc=$null; "
            "try { $word=New-Object -ComObject Word.Application; $word.Visible=$false; "
            "$doc=$word.Documents.Open(%1); $doc.SaveAs(%2,16); } "
            "finally { if ($doc) { $doc.Close(0) }; if ($word) { $word.Quit() } }")
            .arg(powershellLiteral(winPath), powershellLiteral(winTemp));

        QProcess process;
        process.start(QStringLiteral("powershell.exe"),
                      {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                       QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                       QStringLiteral("-Command"), script});
        const bool started = process.waitForStarted(3000);
        const bool finished = started && process.waitForFinished(30000);
        if (!finished && process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(3000);
        }
        if (!started || !finished || process.exitStatus() != QProcess::NormalExit
            || process.exitCode() != 0 || !QFileInfo::exists(tempDocx)) {
            qWarning() << "DOC conversion failed or timed out:" << process.errorString()
                       << process.readAllStandardError();
            return {};
        }
        return extractContent(tempDocx, truncated);
    }

    if (ext == QStringLiteral("docx")) {
        int errorCode = 0;
        const QByteArray nativePath = QDir::toNativeSeparators(filePath).toLocal8Bit();
        zip_t* archive = zip_open(nativePath.constData(), ZIP_RDONLY, &errorCode);
        if (!archive)
            return {};

        zip_stat_t statInfo{};
        if (zip_stat(archive, "word/document.xml", 0, &statInfo) == 0
            && statInfo.size > static_cast<zip_uint64_t>(kMaxDocxXmlBytes)) {
            if (truncated)
                *truncated = true;
            zip_close(archive);
            qWarning() << "DOCX XML exceeds extraction limit:" << filePath;
            return {};
        }

        zip_file_t* entry = zip_fopen(archive, "word/document.xml", 0);
        if (!entry) {
            zip_close(archive);
            return {};
        }

        std::string xmlContent;
        bool docxXmlTruncated = false;
        char buffer[8192];
        zip_int64_t bytesRead = 0;
        while ((bytesRead = zip_fread(entry, buffer, sizeof(buffer))) > 0) {
            if (static_cast<qint64>(xmlContent.size()) + bytesRead > kMaxDocxXmlBytes) {
                docxXmlTruncated = true;
                if (truncated)
                    *truncated = true;
                break;
            }
            xmlContent.append(buffer, static_cast<size_t>(bytesRead));
        }
        zip_fclose(entry);
        zip_close(archive);

        if (docxXmlTruncated)
            return {}; // Avoid indexing a partially-read, invalid XML document.

        pugi::xml_document doc;
        const pugi::xml_parse_result parseResult = doc.load_buffer(
            xmlContent.data(), xmlContent.size(), pugi::parse_default, pugi::encoding_utf8);
        if (!parseResult) {
            qWarning() << "DOCX XML parse failed:" << parseResult.description() << filePath;
            return {};
        }

        QString extracted;
        const pugi::xpath_query paragraphs("//*[local-name()='p']");
        const pugi::xpath_query paragraphText(".//*[local-name()='t']");
        bool stopped = false;
        for (const pugi::xpath_node& paragraph : paragraphs.evaluate_node_set(doc)) {
            bool hasText = false;
            for (const pugi::xpath_node& textNode : paragraphText.evaluate_node_set(paragraph.node())) {
                extracted += QString::fromUtf8(textNode.node().text().get());
                hasText = true;
                if (extracted.size() >= kMaxExtractBytes) {
                    extracted.truncate(static_cast<qsizetype>(kMaxExtractBytes));
                    if (truncated)
                        *truncated = true;
                    stopped = true;
                    break;
                }
            }
            if (hasText)
                extracted += QLatin1Char('\n');
            if (stopped)
                break;
        }
        return normalizedText(extracted);
    }

    // Formats without a text extractor are still catalogued by metadata and filename.
    return {};
}

QString FileManager::extractSummary(const QString& filePath)
{
    bool ignoredTruncation = false;
    return extractContent(filePath, &ignoredTruncation).left(320);
}
