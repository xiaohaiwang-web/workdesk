#include "workdesk/core/FileManager.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QLibrary>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QSet>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QStringList>
#include <QVector>
#include <QTextDocument>
#include <QDebug>

#include <algorithm>
#include <exception>
#include <memory>
#include <string>
#include <fstream>
#include <filesystem>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

extern "C" {
#include <zip.h>
}

#include <pugixml.hpp>
#include <aspose/pdf/document.hpp>
#include <aspose/pdf/page_collection.hpp>
#include <aspose/pdf/resources.hpp>
#include <aspose/pdf/x_image_collection.hpp>
#include <aspose/pdf/text_absorber.hpp>
#include <aspose/pdf/png_device.hpp>
#include <aspose/pdf/resolution.hpp>

// Opaque ABI types from Leptonica and the Tesseract C API. We resolve the
// required exports dynamically, so the project does not need their headers.
struct Pix;
struct TessBaseAPI;
struct TessResultRenderer;

namespace {
constexpr qint64 kMaxExtractChars = 8 * 1024 * 1024;
// PDFs can expand dramatically when indexed with a trigram FTS index. Keep a
// separate, smaller per-PDF text budget and extract one page at a time.
constexpr qint64 kMaxPdfExtractChars = 2 * 1024 * 1024;
constexpr std::size_t kMaxPdfPages = 5000;
constexpr int kPdfOcrNativeTextThreshold = 24;
constexpr int kPdfOcrImagePageTextThreshold = 512;
constexpr int kMaxPdfOcrPages = 80;
constexpr int kPdfOcrPageTimeoutMs = 20000;
constexpr int kPdfOcrTotalTimeoutMs = 5 * 60 * 1000;
constexpr int kPdfOcrDpi = 240;
constexpr int kSummaryPreviewChars = 1000;
constexpr qint64 kMaxXmlEntryBytes = 64 * 1024 * 1024;
constexpr qint64 kMaxOfficeFileBytes = 256 * 1024 * 1024;
constexpr int kComTimeoutMs = 45000;

using ZipPtr = std::unique_ptr<zip_t, decltype(&zip_close)>;

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

void appendBounded(QString& target, const QString& value, bool* truncated)
{
    if (value.isEmpty())
        return;
    const qint64 remaining = kMaxExtractChars - target.size();
    if (remaining <= 0) {
        if (truncated)
            *truncated = true;
        return;
    }
    if (value.size() > remaining) {
        target += value.left(static_cast<qsizetype>(remaining));
        if (truncated)
            *truncated = true;
    } else {
        target += value;
    }
}

void appendPdfBounded(QString& target, const QString& value, bool* truncated)
{
    if (value.isEmpty())
        return;
    const qint64 remaining = kMaxPdfExtractChars - target.size();
    if (remaining <= 0) {
        if (truncated)
            *truncated = true;
        return;
    }
    if (value.size() > remaining) {
        target += value.left(static_cast<qsizetype>(remaining));
        if (truncated)
            *truncated = true;
    } else {
        target += value;
    }
}

QString findTesseractExecutable()
{
    const QString configured = qEnvironmentVariable("WORKDESK_TESSERACT_PATH").trimmed();
    if (!configured.isEmpty() && QFileInfo(configured).isFile())
        return QDir::toNativeSeparators(configured);

    // Prefer the application-private runtime bundled by the Windows installer
    // and portable ZIP. This makes OCR work without a system install or PATH edit.
    const QString appDirectory = QCoreApplication::applicationDirPath();
    const QStringList appLocalCandidates = {
        QDir(appDirectory).filePath(QStringLiteral("ocr/tesseract.exe")),
        QDir(appDirectory).filePath(QStringLiteral("tesseract.exe"))
    };
    for (const QString& candidate : appLocalCandidates) {
        if (QFileInfo(candidate).isFile())
            return QDir::toNativeSeparators(candidate);
    }

    // Developer fallback: use a separately installed Tesseract when the
    // application-private runtime is absent (for example in a development build).
    QString executable = QStandardPaths::findExecutable(QStringLiteral("tesseract.exe"));
    if (executable.isEmpty())
        executable = QStandardPaths::findExecutable(QStringLiteral("tesseract"));
    if (!executable.isEmpty())
        return QDir::toNativeSeparators(executable);

#ifdef Q_OS_WIN
    const QString programFiles = qEnvironmentVariable("ProgramFiles");
    const QString programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)");
    const QStringList candidates = {
        QDir(programFiles).filePath(QStringLiteral("Tesseract-OCR/tesseract.exe")),
        QDir(programFilesX86).filePath(QStringLiteral("Tesseract-OCR/tesseract.exe")),
        QStringLiteral("C:/Program Files/Tesseract-OCR/tesseract.exe"),
        QStringLiteral("C:/Program Files (x86)/Tesseract-OCR/tesseract.exe")
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo(candidate).isFile())
            return QDir::toNativeSeparators(candidate);
    }
#endif
    return {};
}

QString detectTesseractLanguages(const QString& executable)
{
    // Checking the model files avoids spawning a separate tesseract.exe just
    // to list languages. TessBaseAPIInit3 performs the authoritative validation.
    const QString configuredTessdata = qEnvironmentVariable("WORKDESK_TESSDATA_DIR").trimmed();
    const QString tessdataDirectory = configuredTessdata.isEmpty()
        ? QDir(QFileInfo(executable).absolutePath()).filePath(QStringLiteral("tessdata"))
        : configuredTessdata;
    if (!QFileInfo(tessdataDirectory).isDir())
        return {};

    auto hasLanguage = [&tessdataDirectory](const QString& language) {
        return QFileInfo::exists(QDir(tessdataDirectory).filePath(language + QStringLiteral(".traineddata")));
    };

    const QString requested = qEnvironmentVariable("WORKDESK_OCR_LANG").trimmed();
    if (!requested.isEmpty()) {
        const QStringList requestedLanguages = requested.split(QLatin1Char('+'), Qt::SkipEmptyParts);
        if (requestedLanguages.isEmpty())
            return {};
        for (const QString& language : requestedLanguages) {
            if (!QRegularExpression(QStringLiteral("^[a-zA-Z0-9_]+$")).match(language).hasMatch()
                || !hasLanguage(language)) {
                return {};
            }
        }
        return requestedLanguages.join(QLatin1Char('+'));
    }

    if (hasLanguage(QStringLiteral("chi_sim")) && hasLanguage(QStringLiteral("eng")))
        return QStringLiteral("chi_sim+eng");
    if (hasLanguage(QStringLiteral("chi_sim")))
        return QStringLiteral("chi_sim");
    if (hasLanguage(QStringLiteral("eng")))
        return QStringLiteral("eng");
    return {};
}

bool waitForProcess(QProcess& process, int timeoutMs, const std::atomic_bool* cancel)
{
    QElapsedTimer timer;
    timer.start();
    while (process.state() != QProcess::NotRunning) {
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            process.kill();
            process.waitForFinished(1500);
            return false;
        }
        const qint64 remaining = static_cast<qint64>(timeoutMs) - timer.elapsed();
        if (remaining <= 0) {
            process.kill();
            process.waitForFinished(1500);
            return false;
        }
        process.waitForFinished(static_cast<int>(qMin<qint64>(100, remaining)));
    }
    return true;
}

QString runTesseractPage(const QString& executable, const QString& imagePath,
                         const QString& languages, const std::atomic_bool* cancel)
{
    if (cancel && cancel->load(std::memory_order_relaxed))
        return {};

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    const QString configuredTessdata = qEnvironmentVariable("WORKDESK_TESSDATA_DIR").trimmed();
    const QString tessdataDirectory = configuredTessdata.isEmpty()
        ? QDir(QFileInfo(executable).absolutePath()).filePath(QStringLiteral("tessdata"))
        : configuredTessdata;
    process.start(executable, {
        QStringLiteral("--tessdata-dir"), tessdataDirectory,
        imagePath, QStringLiteral("stdout"),
        QStringLiteral("-l"), languages,
        QStringLiteral("--oem"), QStringLiteral("1"),
        QStringLiteral("--psm"), QStringLiteral("3")
    });

    const bool started = process.waitForStarted(5000);
    const bool finished = started && waitForProcess(process, kPdfOcrPageTimeoutMs, cancel);
    if (!started || !finished || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (!(cancel && cancel->load(std::memory_order_relaxed))) {
            qWarning() << "PDF page OCR failed or timed out:" << imagePath
                       << process.readAllStandardError();
        }
        return {};
    }
    return normalizedText(QString::fromUtf8(process.readAllStandardOutput()));
}

#ifdef Q_OS_WIN
class ScopedDllSearchDirectory final
{
public:
    explicit ScopedDllSearchDirectory(const QString& directory)
    {
        std::vector<wchar_t> previousBuffer(32768, L'\0');
        const DWORD previousLength = GetDllDirectoryW(
            static_cast<DWORD>(previousBuffer.size()), previousBuffer.data());
        if (previousLength > 0 && previousLength < static_cast<DWORD>(previousBuffer.size()))
            m_previous.assign(previousBuffer.data(), previousLength);

        const std::wstring nativeDirectory = QDir::toNativeSeparators(directory).toStdWString();
        m_changed = SetDllDirectoryW(nativeDirectory.c_str()) != FALSE;
    }

    ~ScopedDllSearchDirectory()
    {
        if (m_changed)
            SetDllDirectoryW(m_previous.empty() ? nullptr : m_previous.c_str());
    }

    ScopedDllSearchDirectory(const ScopedDllSearchDirectory&) = delete;
    ScopedDllSearchDirectory& operator=(const ScopedDllSearchDirectory&) = delete;

private:
    std::wstring m_previous;
    bool m_changed = false;
};
#endif

class TesseractApiEngine final
{
public:
    TesseractApiEngine() = default;
    TesseractApiEngine(const TesseractApiEngine&) = delete;
    TesseractApiEngine& operator=(const TesseractApiEngine&) = delete;

    ~TesseractApiEngine()
    {
        if (m_handle && m_initialized && m_end)
            m_end(m_handle);
        if (m_handle && m_delete)
            m_delete(m_handle);
        m_handle = nullptr;

        // Keep Leptonica and Tesseract loaded until the API instance is destroyed.
        m_tesseractLibrary.unload();
        m_leptonicaLibrary.unload();
    }

    bool initialize(const QString& executable, const QString& languages)
    {
#ifdef Q_OS_WIN
        const QString directory = QFileInfo(executable).absolutePath();
        const QString configuredTessdata = qEnvironmentVariable("WORKDESK_TESSDATA_DIR").trimmed();
        m_tessdataPath = configuredTessdata.isEmpty()
            ? QDir(directory).filePath(QStringLiteral("tessdata"))
            : configuredTessdata;
        if (!QFileInfo(m_tessdataPath).isDir())
            return false;

        const QString configuredDll = qEnvironmentVariable("WORKDESK_TESSERACT_DLL").trimmed();
        QString tesseractDll = configuredDll;
        if (tesseractDll.isEmpty()) {
            const QStringList candidates = {
                QDir(directory).filePath(QStringLiteral("libtesseract-5.dll")),
                QDir(directory).filePath(QStringLiteral("libtesseract-5.5.dll")),
                QDir(directory).filePath(QStringLiteral("libtesseract.dll"))
            };
            for (const QString& candidate : candidates) {
                if (QFileInfo::exists(candidate)) {
                    tesseractDll = candidate;
                    break;
                }
            }
        }
        if (tesseractDll.isEmpty() || !QFileInfo::exists(tesseractDll))
            return false;

        const QString libraryDirectory = QFileInfo(tesseractDll).absolutePath();
        const QStringList leptonicaCandidates = {
            QDir(libraryDirectory).filePath(QStringLiteral("libleptonica-6.dll")),
            QDir(libraryDirectory).filePath(QStringLiteral("liblept-6.dll")),
            QDir(libraryDirectory).filePath(QStringLiteral("liblept-5.dll")),
            QDir(libraryDirectory).filePath(QStringLiteral("libleptonica-5.dll"))
        };
        QString leptonicaDll;
        for (const QString& candidate : leptonicaCandidates) {
            if (QFileInfo::exists(candidate)) {
                leptonicaDll = candidate;
                break;
            }
        }
        if (leptonicaDll.isEmpty()) {
            qInfo() << "Tesseract Leptonica DLL was not found beside the executable; using CLI OCR.";
            return false;
        }

        // UB Mannheim bundles keep dependent DLLs beside the two main libraries.
        // Temporarily add that directory to Windows' DLL search path while loading.
        [[maybe_unused]] ScopedDllSearchDirectory dllSearchScope(libraryDirectory);

        m_leptonicaLibrary.setFileName(leptonicaDll);
        if (!m_leptonicaLibrary.load()) {
            qWarning() << "Could not load Leptonica runtime:" << m_leptonicaLibrary.errorString();
            return false;
        }

        m_tesseractLibrary.setFileName(tesseractDll);
        if (!m_tesseractLibrary.load()) {
            qWarning() << "Could not load Tesseract runtime:" << m_tesseractLibrary.errorString();
            return false;
        }

        m_pixRead = resolve<PixReadFn>(m_leptonicaLibrary, "pixRead");
        // Prefer the in-memory PNG reader: temporary directories can contain
        // non-ASCII Windows usernames, while Leptonica's filename API uses char*.
        m_pixReadMemPng = resolve<PixReadMemPngFn>(m_leptonicaLibrary, "pixReadMemPng");
        m_pixDestroy = resolve<PixDestroyFn>(m_leptonicaLibrary, "pixDestroy");
        m_pixGetWidth = resolve<PixGetDimensionFn>(m_leptonicaLibrary, "pixGetWidth");
        m_pixGetHeight = resolve<PixGetDimensionFn>(m_leptonicaLibrary, "pixGetHeight");

        m_create = resolve<TessCreateFn>(m_tesseractLibrary, "TessBaseAPICreate");
        m_delete = resolve<TessDeleteFn>(m_tesseractLibrary, "TessBaseAPIDelete");
        m_init = resolve<TessInitFn>(m_tesseractLibrary, "TessBaseAPIInit3");
        m_processPage = resolve<TessProcessPageFn>(m_tesseractLibrary, "TessBaseAPIProcessPage");
        m_getText = resolve<TessGetTextFn>(m_tesseractLibrary, "TessBaseAPIGetUTF8Text");
        m_deleteText = resolve<TessDeleteTextFn>(m_tesseractLibrary, "TessDeleteText");
        m_end = resolve<TessEndFn>(m_tesseractLibrary, "TessBaseAPIEnd");
        m_setPageSegMode = resolve<TessSetPageSegModeFn>(m_tesseractLibrary, "TessBaseAPISetPageSegMode");

        if (!m_pixRead || !m_pixDestroy || !m_pixGetWidth || !m_pixGetHeight
            || !m_create || !m_delete || !m_init || !m_processPage
            || !m_getText || !m_deleteText) {
            qWarning() << "Tesseract/Leptonica runtime is missing required C API exports.";
            return false;
        }

        m_handle = m_create();
        if (!m_handle)
            return false;

        const QByteArray dataPath = QDir::toNativeSeparators(m_tessdataPath).toLocal8Bit();
        const QByteArray languageList = languages.toLocal8Bit();
        if (m_init(m_handle, dataPath.constData(), languageList.constData()) != 0) {
            qWarning() << "Tesseract C API initialization failed for language set:" << languages;
            return false;
        }

        // PSM_AUTO = 3. The engine and language models stay loaded for all pages
        // in this PDF; ProcessPage applies a per-page timeout.
        m_initialized = true;
        if (m_setPageSegMode)
            m_setPageSegMode(m_handle, 3);
        return true;
#else
        Q_UNUSED(executable);
        Q_UNUSED(languages);
        return false;
#endif
    }

    bool isReady() const { return m_handle != nullptr && m_initialized; }

    QString recognize(const QString& imagePath, int pageIndex,
                      const std::atomic_bool* cancel)
    {
#ifdef Q_OS_WIN
        if (!isReady() || (cancel && cancel->load(std::memory_order_relaxed)))
            return {};

        Pix* pix = nullptr;
        if (m_pixReadMemPng) {
            QFile imageFile(imagePath);
            constexpr qint64 kMaxEncodedOcrImageBytes = 128LL * 1024LL * 1024LL;
            if (imageFile.open(QIODevice::ReadOnly)
                && imageFile.size() > 0 && imageFile.size() <= kMaxEncodedOcrImageBytes) {
                const QByteArray encodedPng = imageFile.readAll();
                if (!encodedPng.isEmpty()) {
                    pix = m_pixReadMemPng(
                        reinterpret_cast<const unsigned char*>(encodedPng.constData()),
                        static_cast<std::size_t>(encodedPng.size()));
                }
            }
        }
        if (!pix) {
            // Compatibility fallback for Leptonica builds without pixReadMemPng.
            const QByteArray nativeImagePath = QFile::encodeName(QDir::toNativeSeparators(imagePath));
            pix = m_pixRead(nativeImagePath.constData());
        }
        if (!pix) {
            qWarning() << "Leptonica could not read the rendered PDF page:" << imagePath;
            return {};
        }

        const int width = m_pixGetWidth(pix);
        const int height = m_pixGetHeight(pix);
        constexpr qint64 kMaxOcrPixels = 80LL * 1000LL * 1000LL;
        if (width <= 0 || height <= 0
            || static_cast<qint64>(width) * height > kMaxOcrPixels) {
            m_pixDestroy(&pix);
            qWarning() << "Rendered PDF page exceeds the OCR pixel safety limit:" << imagePath;
            return {};
        }

        if (cancel && cancel->load(std::memory_order_relaxed)) {
            m_pixDestroy(&pix);
            return {};
        }

        const bool processed = m_processPage(
            m_handle, pix, pageIndex, nullptr, nullptr,
            kPdfOcrPageTimeoutMs, nullptr) != 0;
        m_pixDestroy(&pix);

        if (!processed) {
            if (!(cancel && cancel->load(std::memory_order_relaxed)))
                qWarning() << "Tesseract C API failed or reached the per-page timeout:" << imagePath;
            return {};
        }

        if (cancel && cancel->load(std::memory_order_relaxed))
            return {};

        char* rawText = m_getText(m_handle);
        if (!rawText)
            return {};
        const QString text = QString::fromUtf8(rawText);
        m_deleteText(rawText);

        if (cancel && cancel->load(std::memory_order_relaxed))
            return {};
        return normalizedText(text);
#else
        Q_UNUSED(imagePath);
        Q_UNUSED(pageIndex);
        Q_UNUSED(cancel);
        return {};
#endif
    }

private:
#ifdef Q_OS_WIN
    using PixReadFn = Pix* (__cdecl*)(const char*);
    using PixReadMemPngFn = Pix* (__cdecl*)(const unsigned char*, std::size_t);
    using PixDestroyFn = void (__cdecl*)(Pix**);
    using PixGetDimensionFn = int (__cdecl*)(const Pix*);

    using TessCreateFn = TessBaseAPI* (__cdecl*)();
    using TessDeleteFn = void (__cdecl*)(TessBaseAPI*);
    using TessInitFn = int (__cdecl*)(TessBaseAPI*, const char*, const char*);
    using TessProcessPageFn = int (__cdecl*)(TessBaseAPI*, Pix*, int, const char*, const char*, int,
                                              TessResultRenderer*);
    using TessGetTextFn = char* (__cdecl*)(TessBaseAPI*);
    using TessDeleteTextFn = void (__cdecl*)(const char*);
    using TessEndFn = void (__cdecl*)(TessBaseAPI*);
    using TessSetPageSegModeFn = void (__cdecl*)(TessBaseAPI*, int);
#else
    using PixReadFn = Pix* (*) (const char*);
    using PixReadMemPngFn = Pix* (*) (const unsigned char*, std::size_t);
    using PixDestroyFn = void (*) (Pix**);
    using PixGetDimensionFn = int (*) (const Pix*);

    using TessCreateFn = TessBaseAPI* (*)();
    using TessDeleteFn = void (*)(TessBaseAPI*);
    using TessInitFn = int (*)(TessBaseAPI*, const char*, const char*);
    using TessProcessPageFn = int (*)(TessBaseAPI*, Pix*, int, const char*, const char*, int,
                                       TessResultRenderer*);
    using TessGetTextFn = char* (*)(TessBaseAPI*);
    using TessDeleteTextFn = void (*)(const char*);
    using TessEndFn = void (*)(TessBaseAPI*);
    using TessSetPageSegModeFn = void (*)(TessBaseAPI*, int);
#endif

    template <typename Function>
    static Function resolve(QLibrary& library, const char* symbol)
    {
        return reinterpret_cast<Function>(library.resolve(symbol));
    }

    QLibrary m_leptonicaLibrary;
    QLibrary m_tesseractLibrary;
    QString m_tessdataPath;
    TessBaseAPI* m_handle = nullptr;
    bool m_initialized = false;
    PixReadFn m_pixRead = nullptr;
    PixReadMemPngFn m_pixReadMemPng = nullptr;
    PixDestroyFn m_pixDestroy = nullptr;
    PixGetDimensionFn m_pixGetWidth = nullptr;
    PixGetDimensionFn m_pixGetHeight = nullptr;
    TessCreateFn m_create = nullptr;
    TessDeleteFn m_delete = nullptr;
    TessInitFn m_init = nullptr;
    TessProcessPageFn m_processPage = nullptr;
    TessGetTextFn m_getText = nullptr;
    TessDeleteTextFn m_deleteText = nullptr;
    TessEndFn m_end = nullptr;
    TessSetPageSegModeFn m_setPageSegMode = nullptr;
};

QString extractPdfContent(const QString& filePath, bool* truncated,
                          const std::atomic_bool* cancel)
{
    const QFileInfo info(filePath);
    if (info.size() > kMaxOfficeFileBytes) {
        if (truncated)
            *truncated = true;
        qWarning() << "PDF exceeds the configured extraction size limit:" << filePath;
        return {};
    }

    try {
        Aspose::Pdf::Document document(filePath.toStdString());
        const std::size_t pageCount = document.Pages().Count();
        if (pageCount == 0)
            return {};

        const bool ocrEnabled = qEnvironmentVariable("WORKDESK_PDF_OCR", QStringLiteral("1")) != QStringLiteral("0");
        QString tesseractExecutable;
        QString ocrLanguages;
        bool ocrLookupDone = false;
        bool ocrUnavailableLogged = false;
        bool ocrBudgetReached = false;
        bool ocrTimerStarted = false;
        int ocrPageCount = 0;
        QElapsedTimer ocrTimer;
        QTemporaryDir ocrTempDir;
        std::unique_ptr<Aspose::Pdf::Devices::PngDevice> pngDevice;

        QString result;
        result.reserve(64 * 1024);
        const std::size_t pagesToRead = std::min(pageCount, kMaxPdfPages);
        if (pageCount > kMaxPdfPages && truncated)
            *truncated = true;

        std::unique_ptr<TesseractApiEngine> tesseractApi;

        for (std::size_t pageNumber = 1; pageNumber <= pagesToRead; ++pageNumber) {
            if (cancel && cancel->load(std::memory_order_relaxed))
                break;
            if (result.size() >= kMaxPdfExtractChars) {
                if (truncated)
                    *truncated = true;
                break;
            }

            auto page = document.Pages()[static_cast<int>(pageNumber)];
            Aspose::Pdf::Text::TextAbsorber absorber;
            absorber.Visit(page);
            const std::string rawText = absorber.Text();
            QString pageText = normalizedText(QString::fromUtf8(
                rawText.data(), static_cast<qsizetype>(rawText.size())));

            // Do not rely on text length alone: patent pages often have a small
            // native header/footer plus a full-page scanned image. A substantial
            // image resource together with sparse native text is an OCR candidate.
            std::size_t pageImageCount = 0;
            try {
                pageImageCount = page.Resources().Images().Count();
            } catch (const std::exception&) {
                pageImageCount = 0;
            }
            const bool sparseNativeText = pageText.size() < kPdfOcrNativeTextThreshold;
            const bool imageDominatedPage = pageImageCount > 0
                && pageText.size() < kPdfOcrImagePageTextThreshold;
            const bool needsOcr = sparseNativeText || imageDominatedPage;

            if (ocrEnabled && !ocrBudgetReached && needsOcr) {
                if (!ocrLookupDone) {
                    ocrLookupDone = true;
                    tesseractExecutable = findTesseractExecutable();
                    if (!tesseractExecutable.isEmpty()) {
                        ocrLanguages = detectTesseractLanguages(tesseractExecutable);
                        if (!ocrLanguages.isEmpty()) {
                            tesseractApi = std::make_unique<TesseractApiEngine>();
                            if (!tesseractApi->initialize(tesseractExecutable, ocrLanguages)) {
                                qInfo() << "Tesseract C API unavailable; using the command-line fallback.";
                                tesseractApi.reset();
                            } else {
                                qInfo() << "Tesseract C API initialized once for this PDF:" << ocrLanguages;
                            }
                        }
                    }
                }

                if (tesseractExecutable.isEmpty() || ocrLanguages.isEmpty() || !ocrTempDir.isValid()) {
                    if (truncated)
                        *truncated = true;
                    if (!ocrUnavailableLogged) {
                        qWarning() << "Scanned PDF page detected, but optional OCR is unavailable."
                                   << "Install Tesseract OCR with chi_sim and/or eng data, or set"
                                   << "WORKDESK_TESSERACT_PATH and WORKDESK_OCR_LANG."
                                   << filePath;
                        ocrUnavailableLogged = true;
                    }
                } else if (ocrPageCount >= kMaxPdfOcrPages) {
                    if (truncated)
                        *truncated = true;
                    ocrBudgetReached = true;
                    qWarning() << "PDF OCR page limit reached; remaining image-only pages will be skipped:" << filePath;
                } else {
                    if (!ocrTimerStarted) {
                        ocrTimer.start();
                        ocrTimerStarted = true;
                    }
                    if (ocrTimer.elapsed() >= kPdfOcrTotalTimeoutMs) {
                        if (truncated)
                            *truncated = true;
                        ocrBudgetReached = true;
                        qWarning() << "PDF OCR time budget reached; remaining image-only pages will be skipped:" << filePath;
                    } else {
                        const QString imagePath = ocrTempDir.filePath(
                            QStringLiteral("page-%1.png").arg(pageNumber, 6, 10, QLatin1Char('0')));
                        try {
                            if (!pngDevice) {
                                pngDevice = std::make_unique<Aspose::Pdf::Devices::PngDevice>(
                                    Aspose::Pdf::Devices::Resolution(kPdfOcrDpi));
                            }
                            std::ofstream imageFile(std::filesystem::path(imagePath.toStdWString()),
                                                    std::ios::binary | std::ios::trunc);
                            if (imageFile.is_open()) {
                                pngDevice->Process(page, imageFile);
                                imageFile.close();
                                if (QFileInfo::exists(imagePath)) {
                                    ++ocrPageCount;
                                    const QString ocrText = tesseractApi && tesseractApi->isReady()
                                        ? tesseractApi->recognize(imagePath, static_cast<int>(pageNumber - 1), cancel)
                                        : runTesseractPage(tesseractExecutable, imagePath, ocrLanguages, cancel);
                                    if (!ocrText.isEmpty()) {
                                        // Keep any native text (for example page labels or headers)
                                        // and append OCR text from the raster page.
                                        if (!pageText.isEmpty())
                                            pageText.append(QLatin1Char('\n'));
                                        pageText.append(ocrText);
                                    }
                                }
                                QFile::remove(imagePath);
                            } else {
                                qWarning() << "Could not create temporary PDF page image:" << imagePath;
                            }
                        } catch (const std::exception& renderError) {
                            if (truncated)
                                *truncated = true;
                            ocrBudgetReached = true;
                            qWarning() << "PDF page rendering for OCR failed; continue with native text only:"
                                       << renderError.what() << filePath;
                        }
                    }
                }
            }

            if (!pageText.isEmpty()) {
                const QString pagePrefix = QStringLiteral("\n[PDF 第 %1 页]\n").arg(pageNumber);
                appendPdfBounded(result, pagePrefix, truncated);
                appendPdfBounded(result, pageText, truncated);
                appendPdfBounded(result, QStringLiteral("\n"), truncated);
            }
        }

        return normalizedText(result);
    } catch (const std::exception& error) {
        qWarning() << "PDF extraction failed:" << error.what() << filePath;
        return {};
    }
}

QString extensionOf(const QString& filePath)
{
    return QFileInfo(filePath).suffix().toLower();
}

bool containsExtension(const QSet<QString>& extensions, const QString& extension)
{
    return extensions.contains(extension.toLower());
}

const QSet<QString>& imageExtensions()
{
    static const QSet<QString> values = {
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("jpe"),
        QStringLiteral("png"), QStringLiteral("gif"), QStringLiteral("bmp"),
        QStringLiteral("dib"), QStringLiteral("tif"), QStringLiteral("tiff"),
        QStringLiteral("jfif"), QStringLiteral("pjpeg"), QStringLiteral("pjp"),
        QStringLiteral("apng"), QStringLiteral("tga"), QStringLiteral("exr"),
        QStringLiteral("hdr"), QStringLiteral("webp"), QStringLiteral("svg"),
        QStringLiteral("svgz"), QStringLiteral("ico"), QStringLiteral("icns"),
        QStringLiteral("heic"), QStringLiteral("heif"), QStringLiteral("avif"),
        QStringLiteral("jxl"), QStringLiteral("wmf"), QStringLiteral("emf"),
        QStringLiteral("raw"), QStringLiteral("cr2"),
        QStringLiteral("cr3"), QStringLiteral("nef"), QStringLiteral("arw"),
        QStringLiteral("dng"), QStringLiteral("orf"), QStringLiteral("rw2"),
        QStringLiteral("pef"), QStringLiteral("srw"), QStringLiteral("jif"),
        QStringLiteral("jp2"), QStringLiteral("j2k"), QStringLiteral("jpf"),
        QStringLiteral("jpx"), QStringLiteral("jpm"), QStringLiteral("mj2"),
        QStringLiteral("pcx"), QStringLiteral("ppm"), QStringLiteral("pgm"),
        QStringLiteral("pbm"), QStringLiteral("pnm"), QStringLiteral("wbmp"),
        QStringLiteral("xbm"), QStringLiteral("xpm"), QStringLiteral("mng"),
        QStringLiteral("dds"), QStringLiteral("sgi"), QStringLiteral("rgb"),
        QStringLiteral("rgba"), QStringLiteral("ras"), QStringLiteral("sun"),
        QStringLiteral("pic"), QStringLiteral("jng"), QStringLiteral("pcd"),
        QStringLiteral("pct"), QStringLiteral("pict"), QStringLiteral("odi"),
        QStringLiteral("jxr"), QStringLiteral("hdp"), QStringLiteral("wdp"),
        QStringLiteral("wmz"), QStringLiteral("emz"), QStringLiteral("crw"),
        QStringLiteral("kdc"), QStringLiteral("dcr"), QStringLiteral("mrw"),
        QStringLiteral("nrw"), QStringLiteral("rwl"), QStringLiteral("3fr"),
        QStringLiteral("iiq"), QStringLiteral("mos"), QStringLiteral("mef"),
        QStringLiteral("cap"), QStringLiteral("ptx"), QStringLiteral("x3f"),
        QStringLiteral("bay"), QStringLiteral("erf"),
        // Cursor, texture and additional interchange formats. These are catalogued only.
        QStringLiteral("cur"), QStringLiteral("ani"), QStringLiteral("qoi"),
        QStringLiteral("ktx"), QStringLiteral("ktx2"), QStringLiteral("basis"),
        QStringLiteral("astc"), QStringLiteral("pvr"),
        // Additional camera RAW variants.
        QStringLiteral("raf"), QStringLiteral("ari"), QStringLiteral("dcs"),
        QStringLiteral("drf"), QStringLiteral("eip"), QStringLiteral("fff"),
        QStringLiteral("gpr"), QStringLiteral("k25"), QStringLiteral("mdc"),
        QStringLiteral("pxn"), QStringLiteral("rwz"), QStringLiteral("sr2"),
        QStringLiteral("srf"), QStringLiteral("x3f"),
        // Medical/scientific image containers (metadata-only in this release).
        QStringLiteral("dcm"), QStringLiteral("dicom")
    };
    return values;
}

const QSet<QString>& wordExtensions()
{
    static const QSet<QString> values = {
        QStringLiteral("doc"), QStringLiteral("docx"), QStringLiteral("docm"),
        QStringLiteral("dot"), QStringLiteral("dotx"), QStringLiteral("dotm"),
        QStringLiteral("odt"), QStringLiteral("ott"), QStringLiteral("fodt"),
        QStringLiteral("odm"), QStringLiteral("oth"), QStringLiteral("rtf"), QStringLiteral("sxw"),
        QStringLiteral("wps"), QStringLiteral("wpt"), QStringLiteral("wpd"),
        QStringLiteral("wp"), QStringLiteral("lwp"), QStringLiteral("docb"),
        QStringLiteral("sdw"), QStringLiteral("stw")
    };
    return values;
}

const QSet<QString>& excelExtensions()
{
    static const QSet<QString> values = {
        QStringLiteral("xls"), QStringLiteral("xlsx"), QStringLiteral("xlsm"),
        QStringLiteral("xlsb"), QStringLiteral("xlt"), QStringLiteral("xltx"),
        QStringLiteral("xltm"), QStringLiteral("xla"), QStringLiteral("xlam"),
        QStringLiteral("ods"), QStringLiteral("ots"), QStringLiteral("fods"),
        QStringLiteral("sxc"), QStringLiteral("odc"), QStringLiteral("et"), QStringLiteral("ett"),
        QStringLiteral("csv"), QStringLiteral("tsv"), QStringLiteral("dif"),
        QStringLiteral("slk"), QStringLiteral("prn"), QStringLiteral("dbf"),
        QStringLiteral("xlw"), QStringLiteral("xlm"), QStringLiteral("xlc"),
        QStringLiteral("xll"), QStringLiteral("xlb"),
        QStringLiteral("wks"), QStringLiteral("wk1"), QStringLiteral("wk2"),
        QStringLiteral("wk3"), QStringLiteral("wk4"), QStringLiteral("wq1"),
        QStringLiteral("123"), QStringLiteral("qpw"), QStringLiteral("wb1"),
        QStringLiteral("wb2"), QStringLiteral("wb3"), QStringLiteral("sdc"),
        QStringLiteral("stc")
    };
    return values;
}

const QSet<QString>& presentationExtensions()
{
    static const QSet<QString> values = {
        QStringLiteral("ppt"), QStringLiteral("pptx"), QStringLiteral("pptm"),
        QStringLiteral("pps"), QStringLiteral("ppsx"), QStringLiteral("ppsm"),
        QStringLiteral("pot"), QStringLiteral("potx"), QStringLiteral("potm"),
        QStringLiteral("sldx"), QStringLiteral("sldm"), QStringLiteral("ppam"),
        QStringLiteral("odp"), QStringLiteral("otp"), QStringLiteral("fodp"),
        QStringLiteral("sxi"), QStringLiteral("thmx"),
        QStringLiteral("dps"), QStringLiteral("dpt"), QStringLiteral("ppa"),
        QStringLiteral("sdd"), QStringLiteral("sti")
    };
    return values;
}

const QSet<QString>& diagramExtensions()
{
    static const QSet<QString> values = {
        QStringLiteral("vsd"), QStringLiteral("vsdx"), QStringLiteral("vsdm"),
        QStringLiteral("vss"), QStringLiteral("vssx"), QStringLiteral("vssm"),
        QStringLiteral("vst"), QStringLiteral("vstx"), QStringLiteral("vstm"),
        QStringLiteral("odg"), QStringLiteral("otg"), QStringLiteral("fodg"),
        QStringLiteral("sxd"),
        QStringLiteral("ai"), QStringLiteral("eps"), QStringLiteral("psd"),
        QStringLiteral("xcf"), QStringLiteral("indd"), QStringLiteral("sketch"),
        QStringLiteral("fig"), QStringLiteral("afdesign"), QStringLiteral("afphoto"),
        QStringLiteral("cdr"), QStringLiteral("psb"), QStringLiteral("kra"),
        QStringLiteral("ora"), QStringLiteral("clip"), QStringLiteral("sai"),
        QStringLiteral("sai2"), QStringLiteral("procreate"), QStringLiteral("afpub"),
        QStringLiteral("indt"), QStringLiteral("indb"), QStringLiteral("gbr"),
        QStringLiteral("abr"), QStringLiteral("pat"),
        QStringLiteral("blend"), QStringLiteral("dwg"), QStringLiteral("dxf"),
        QStringLiteral("dgn"), QStringLiteral("ifc"), QStringLiteral("3dm"),
        QStringLiteral("3ds"), QStringLiteral("obj"), QStringLiteral("fbx"),
        QStringLiteral("stl"), QStringLiteral("step"), QStringLiteral("stp"),
        QStringLiteral("iges"), QStringLiteral("igs"), QStringLiteral("skp"),
        QStringLiteral("sldprt"), QStringLiteral("sldasm"), QStringLiteral("slddrw"),
        QStringLiteral("ipt"), QStringLiteral("iam"), QStringLiteral("idw"),
        QStringLiteral("f3d"), QStringLiteral("f3z"), QStringLiteral("sat"),
        QStringLiteral("sab"), QStringLiteral("3mf"), QStringLiteral("sda"),
        QStringLiteral("std")
    };
    return values;
}

const QSet<QString>& otherOfficeExtensions()
{
    static const QSet<QString> values = {
        QStringLiteral("pub"), QStringLiteral("mpp"), QStringLiteral("mpt"),
        QStringLiteral("mpx"), QStringLiteral("one"), QStringLiteral("onepkg"),
        QStringLiteral("onecache"), QStringLiteral("onetoc2"), QStringLiteral("xsn"),
        QStringLiteral("xsf"), QStringLiteral("odf"), QStringLiteral("accdw")
    };
    return values;
}

const QSet<QString>& emailExtensions()
{
    static const QSet<QString> values = {
        QStringLiteral("eml"), QStringLiteral("emlx"), QStringLiteral("mbox"),
        QStringLiteral("msg"), QStringLiteral("oft"), QStringLiteral("pst"),
        QStringLiteral("ost"), QStringLiteral("olm"), QStringLiteral("ics"),
        QStringLiteral("vcs"), QStringLiteral("vcf")
    };
    return values;
}

const QSet<QString>& ebookExtensions()
{
    static const QSet<QString> values = {
        QStringLiteral("epub"), QStringLiteral("mobi"), QStringLiteral("azw"),
        QStringLiteral("azw3"), QStringLiteral("fb2"), QStringLiteral("djvu"),
        QStringLiteral("cbz"), QStringLiteral("cbr")
    };
    return values;
}

bool isPlainTextExtension(const QString& ext)
{
    static const QSet<QString> extensions = {
        QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("markdown"),
        QStringLiteral("rst"), QStringLiteral("adoc"), QStringLiteral("asciidoc"),
        QStringLiteral("cpp"), QStringLiteral("cc"), QStringLiteral("cxx"),
        QStringLiteral("c"), QStringLiteral("h"), QStringLiteral("hh"),
        QStringLiteral("hpp"), QStringLiteral("hxx"), QStringLiteral("py"),
        QStringLiteral("pyi"), QStringLiteral("js"), QStringLiteral("jsx"),
        QStringLiteral("mjs"), QStringLiteral("cjs"), QStringLiteral("ts"),
        QStringLiteral("tsx"), QStringLiteral("vue"), QStringLiteral("json"),
        QStringLiteral("jsonc"), QStringLiteral("xml"), QStringLiteral("xhtml"),
        QStringLiteral("html"), QStringLiteral("htm"), QStringLiteral("css"),
        QStringLiteral("scss"), QStringLiteral("sass"), QStringLiteral("less"),
        QStringLiteral("csv"), QStringLiteral("tsv"), QStringLiteral("dif"),
        QStringLiteral("slk"), QStringLiteral("log"), QStringLiteral("ini"),
        QStringLiteral("cfg"), QStringLiteral("conf"), QStringLiteral("yaml"),
        QStringLiteral("yml"), QStringLiteral("toml"), QStringLiteral("properties"),
        QStringLiteral("sql"), QStringLiteral("odc"), QStringLiteral("java"), QStringLiteral("kt"),
        QStringLiteral("kts"), QStringLiteral("cs"), QStringLiteral("go"),
        QStringLiteral("rs"), QStringLiteral("php"), QStringLiteral("rb"),
        QStringLiteral("sh"), QStringLiteral("bash"), QStringLiteral("zsh"),
        QStringLiteral("ps1"), QStringLiteral("bat"), QStringLiteral("cmd"),
        QStringLiteral("cmake"), QStringLiteral("gradle"), QStringLiteral("make"),
        QStringLiteral("tex"), QStringLiteral("bib"), QStringLiteral("eml"),
        QStringLiteral("ics"), QStringLiteral("vcf"), QStringLiteral("properties"),
        QStringLiteral("env"), QStringLiteral("gitignore"), QStringLiteral("dockerfile"),
        QStringLiteral("conf"), QStringLiteral("lock"), QStringLiteral("diff"),
        QStringLiteral("patch"), QStringLiteral("asm"), QStringLiteral("s"),
        QStringLiteral("prn"), QStringLiteral("iqy"), QStringLiteral("oqy"),
        QStringLiteral("dqy"), QStringLiteral("mbox"), QStringLiteral("emlx"),
        QStringLiteral("vcs"),
        QStringLiteral("swift"), QStringLiteral("m"), QStringLiteral("mm"),
        QStringLiteral("r"), QStringLiteral("scala"), QStringLiteral("pl"),
        QStringLiteral("lua"), QStringLiteral("dart"), QStringLiteral("ex"),
        QStringLiteral("exs"), QStringLiteral("erl"), QStringLiteral("hrl"),
        QStringLiteral("clj"), QStringLiteral("cljs"), QStringLiteral("fs"),
        QStringLiteral("fsx"), QStringLiteral("vb"), QStringLiteral("vbs"),
        QStringLiteral("psm1"), QStringLiteral("psd1"), QStringLiteral("ipynb")
    };
    return extensions.contains(ext);
}

QString decodeUtf8Bounded(const QByteArray& bytes, bool* truncated)
{
    QByteArray bounded = bytes;
    const bool wasTruncated = bounded.size() > kMaxExtractChars;
    if (wasTruncated) {
        bounded.truncate(static_cast<qsizetype>(kMaxExtractChars));
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
            // Legacy Windows documents frequently use the active ANSI code page.
            decoded = QString::fromLocal8Bit(bounded);
        }
    }
    return normalizedText(decoded);
}

ZipPtr openZip(const QString& filePath)
{
    // libzip expects filesystem paths in UTF-8 on Windows and POSIX.
    const QByteArray path = filePath.toUtf8();
    int errorCode = 0;
    return ZipPtr(zip_open(path.constData(), ZIP_RDONLY, &errorCode), &zip_close);
}

QByteArray readZipEntry(zip_t* archive, const QString& entryName, qint64 maxBytes,
                        bool* truncated = nullptr)
{
    if (truncated)
        *truncated = false;
    if (!archive)
        return {};

    const QByteArray name = entryName.toUtf8();
    const zip_int64_t index = zip_name_locate(archive, name.constData(), 0);
    if (index < 0)
        return {};

    zip_stat_t info{};
    zip_stat_init(&info);
    if (zip_stat_index(archive, static_cast<zip_uint64_t>(index), 0, &info) != 0)
        return {};
    if (info.size > static_cast<zip_uint64_t>(maxBytes)) {
        if (truncated)
            *truncated = true;
        return {};
    }

    zip_file_t* file = zip_fopen_index(archive, static_cast<zip_uint64_t>(index), 0);
    if (!file)
        return {};

    QByteArray data;
    data.reserve(static_cast<qsizetype>(qMin<zip_uint64_t>(info.size, static_cast<zip_uint64_t>(maxBytes))));
    char buffer[16384];
    bool failed = false;
    zip_int64_t count = 0;
    while ((count = zip_fread(file, buffer, sizeof(buffer))) > 0) {
        if (static_cast<qint64>(data.size()) + count > maxBytes) {
            if (truncated)
                *truncated = true;
            failed = true;
            break;
        }
        data.append(buffer, static_cast<qsizetype>(count));
    }
    if (count < 0)
        failed = true;
    zip_fclose(file);
    return failed ? QByteArray{} : data;
}

QStringList listZipEntries(zip_t* archive)
{
    QStringList result;
    if (!archive)
        return result;
    const zip_int64_t count = zip_get_num_entries(archive, 0);
    for (zip_int64_t i = 0; i < count; ++i) {
        const char* name = zip_get_name(archive, static_cast<zip_uint64_t>(i), ZIP_FL_ENC_GUESS);
        if (name)
            result.append(QString::fromUtf8(name));
    }
    return result;
}

int trailingNumber(const QString& value)
{
    static const QRegularExpression number(QStringLiteral("(\\d+)(?=\\.[^.]+$)"));
    const auto match = number.match(value);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

void sortZipEntries(QStringList& entries)
{
    std::sort(entries.begin(), entries.end(), [](const QString& left, const QString& right) {
        const int leftNumber = trailingNumber(left);
        const int rightNumber = trailingNumber(right);
        if (leftNumber != rightNumber)
            return leftNumber < rightNumber;
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
}

void appendPugiText(pugi::xml_node node, QString& output)
{
    if (node.type() == pugi::node_pcdata || node.type() == pugi::node_cdata) {
        output += QString::fromUtf8(node.value());
        return;
    }
    for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling())
        appendPugiText(child, output);
}

QString xmlParagraphs(const QByteArray& xml, bool* parseOk = nullptr)
{
    pugi::xml_document document;
    const pugi::xml_parse_result result = document.load_buffer(
        xml.constData(), static_cast<size_t>(xml.size()), pugi::parse_default, pugi::encoding_utf8);
    if (parseOk)
        *parseOk = static_cast<bool>(result);
    if (!result)
        return {};

    QString output;
    const pugi::xpath_query paragraphs("//*[local-name()='p' or local-name()='h']");
    const pugi::xpath_node_set nodes = paragraphs.evaluate_node_set(document);
    if (!nodes.empty()) {
        for (const pugi::xpath_node& item : nodes) {
            QString line;
            appendPugiText(item.node(), line);
            if (!line.trimmed().isEmpty()) {
                output += line;
                output += QLatin1Char('\n');
            }
            if (output.size() >= kMaxExtractChars)
                break;
        }
    } else {
        appendPugiText(document, output);
    }
    return output;
}

QString xmlNamedText(const QByteArray& xml, const QString& nodeLocalName, bool* parseOk = nullptr)
{
    pugi::xml_document document;
    const pugi::xml_parse_result result = document.load_buffer(
        xml.constData(), static_cast<size_t>(xml.size()), pugi::parse_default, pugi::encoding_utf8);
    if (parseOk)
        *parseOk = static_cast<bool>(result);
    if (!result)
        return {};

    const QString queryText = QStringLiteral("//*[local-name()='%1']").arg(nodeLocalName);
    const QByteArray queryUtf8 = queryText.toUtf8();
    pugi::xpath_query query(queryUtf8.constData());
    QString output;
    for (const pugi::xpath_node& item : query.evaluate_node_set(document)) {
        QString line;
        appendPugiText(item.node(), line);
        const QString attribute = QString::fromUtf8(item.node().attribute("UnicodeString").value());
        if (!attribute.isEmpty())
            line = attribute;
        if (!line.trimmed().isEmpty()) {
            output += line;
            output += QLatin1Char('\n');
        }
        if (output.size() >= kMaxExtractChars)
            break;
    }
    return output;
}

QString extractWordXml(zip_t* archive, bool* truncated)
{
    QStringList entries = listZipEntries(archive).filter(QRegularExpression(
        QStringLiteral("^word/(document|header\\d*|footer\\d*|footnotes|endnotes|comments)\\.xml$")));
    sortZipEntries(entries);

    QString output;
    for (const QString& entry : entries) {
        bool entryTruncated = false;
        const QByteArray xml = readZipEntry(archive, entry, kMaxXmlEntryBytes, &entryTruncated);
        if (entryTruncated) {
            if (truncated)
                *truncated = true;
            continue;
        }
        if (xml.isEmpty())
            continue;
        bool parseOk = false;
        const QString text = xmlParagraphs(xml, &parseOk);
        if (!parseOk)
            qWarning() << "Could not parse Word XML part:" << entry;
        appendBounded(output, text + QLatin1Char('\n'), truncated);
        if (output.size() >= kMaxExtractChars)
            break;
    }
    return output;
}

QStringList extractSharedStrings(zip_t* archive, bool* truncated)
{
    QStringList values;
    bool partTruncated = false;
    const QByteArray xml = readZipEntry(archive, QStringLiteral("xl/sharedStrings.xml"),
                                        kMaxXmlEntryBytes, &partTruncated);
    if (partTruncated) {
        if (truncated)
            *truncated = true;
        return values;
    }
    if (xml.isEmpty())
        return values;

    pugi::xml_document document;
    if (!document.load_buffer(xml.constData(), static_cast<size_t>(xml.size()),
                              pugi::parse_default, pugi::encoding_utf8)) {
        qWarning() << "Could not parse Excel shared strings";
        return values;
    }

    const pugi::xpath_query strings("//*[local-name()='si']");
    const pugi::xpath_query textNodes(".//*[local-name()='t']");
    for (const pugi::xpath_node& item : strings.evaluate_node_set(document)) {
        QString value;
        for (const pugi::xpath_node& textNode : textNodes.evaluate_node_set(item.node()))
            value += QString::fromUtf8(textNode.node().text().get());
        values.append(value);
        if (values.size() >= 1000000) {
            if (truncated)
                *truncated = true;
            break;
        }
    }
    return values;
}

QString extractExcelWorksheet(const QByteArray& xml, const QStringList& sharedStrings,
                              bool* parseOk)
{
    pugi::xml_document document;
    const pugi::xml_parse_result result = document.load_buffer(
        xml.constData(), static_cast<size_t>(xml.size()), pugi::parse_default, pugi::encoding_utf8);
    if (parseOk)
        *parseOk = static_cast<bool>(result);
    if (!result)
        return {};

    QString output;
    const pugi::xpath_query rows("//*[local-name()='row']");
    const pugi::xpath_query values(".//*[local-name()='v']");
    const pugi::xpath_query formulas(".//*[local-name()='f']");
    const pugi::xpath_query inlineText(".//*[local-name()='is']//*[local-name()='t']");

    for (const pugi::xpath_node& rowItem : rows.evaluate_node_set(document)) {
        QStringList cells;
        const pugi::xml_node row = rowItem.node();
        for (pugi::xml_node cell = row.first_child(); cell; cell = cell.next_sibling()) {
            if (QString::fromUtf8(cell.name()).section(QLatin1Char(':'), -1) != QStringLiteral("c"))
                continue;

            const QString type = QString::fromUtf8(cell.attribute("t").value());
            const QString reference = QString::fromUtf8(cell.attribute("r").value());
            QString value;
            const pugi::xpath_node_set valueNodes = values.evaluate_node_set(cell);
            if (!valueNodes.empty())
                value = QString::fromUtf8(valueNodes.first().node().text().get());

            if (type == QStringLiteral("s")) {
                bool ok = false;
                const int stringIndex = value.toInt(&ok);
                value = (ok && stringIndex >= 0 && stringIndex < sharedStrings.size())
                    ? sharedStrings.at(stringIndex) : QString();
            } else if (type == QStringLiteral("inlineStr")) {
                value.clear();
                for (const pugi::xpath_node& textNode : inlineText.evaluate_node_set(cell))
                    value += QString::fromUtf8(textNode.node().text().get());
            } else if (type == QStringLiteral("b")) {
                value = value == QStringLiteral("1") ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
            }

            if (value.isEmpty()) {
                const pugi::xpath_node_set formulaNodes = formulas.evaluate_node_set(cell);
                if (!formulaNodes.empty())
                    value = QStringLiteral("=") + QString::fromUtf8(formulaNodes.first().node().text().get());
            }
            if (!value.isEmpty())
                cells.append(reference.isEmpty() ? value : reference + QStringLiteral(": ") + value);
        }
        if (!cells.isEmpty()) {
            output += cells.join(QStringLiteral(" | "));
            output += QLatin1Char('\n');
        }
        if (output.size() >= kMaxExtractChars)
            break;
    }
    return output;
}

QString extractExcelXml(zip_t* archive, bool* truncated)
{
    const QStringList sharedStrings = extractSharedStrings(archive, truncated);
    QStringList sheets;
    const QStringList allEntries = listZipEntries(archive);
    for (const QString& entry : allEntries) {
        if (QRegularExpression(QStringLiteral("^xl/worksheets/sheet\\d+\\.xml$")
                              ).match(entry).hasMatch()) {
            sheets.append(entry);
        }
    }
    sortZipEntries(sheets);

    QString output;
    int sheetNumber = 0;
    for (const QString& sheet : sheets) {
        ++sheetNumber;
        bool partTruncated = false;
        const QByteArray xml = readZipEntry(archive, sheet, kMaxXmlEntryBytes, &partTruncated);
        if (partTruncated) {
            if (truncated)
                *truncated = true;
            continue;
        }
        if (xml.isEmpty())
            continue;
        bool parseOk = false;
        const QString sheetText = extractExcelWorksheet(xml, sharedStrings, &parseOk);
        if (!parseOk) {
            qWarning() << "Could not parse Excel worksheet:" << sheet;
            continue;
        }
        if (!sheetText.trimmed().isEmpty()) {
            appendBounded(output, QStringLiteral("工作表 %1\n").arg(sheetNumber), truncated);
            appendBounded(output, sheetText, truncated);
        }
        if (output.size() >= kMaxExtractChars)
            break;
    }
    return output;
}

QString extractPowerPointXml(zip_t* archive, bool* truncated)
{
    const QStringList allEntries = listZipEntries(archive);
    QStringList slides;
    QStringList notes;
    const QRegularExpression slidePattern(QStringLiteral("^ppt/slides/slide\\d+\\.xml$"));
    const QRegularExpression notePattern(QStringLiteral("^ppt/notesSlides/notesSlide\\d+\\.xml$"));
    for (const QString& entry : allEntries) {
        if (slidePattern.match(entry).hasMatch())
            slides.append(entry);
        else if (notePattern.match(entry).hasMatch())
            notes.append(entry);
    }
    sortZipEntries(slides);
    sortZipEntries(notes);

    QString output;
    int index = 0;
    for (const QString& slide : slides) {
        ++index;
        bool partTruncated = false;
        const QByteArray xml = readZipEntry(archive, slide, kMaxXmlEntryBytes, &partTruncated);
        if (partTruncated) {
            if (truncated)
                *truncated = true;
            continue;
        }
        if (xml.isEmpty())
            continue;
        bool parseOk = false;
        const QString text = xmlParagraphs(xml, &parseOk);
        if (!parseOk)
            qWarning() << "Could not parse PowerPoint slide:" << slide;
        if (!text.trimmed().isEmpty()) {
            appendBounded(output, QStringLiteral("幻灯片 %1\n").arg(index), truncated);
            appendBounded(output, text, truncated);
        }
        if (output.size() >= kMaxExtractChars)
            return output;
    }

    for (const QString& note : notes) {
        bool partTruncated = false;
        const QByteArray xml = readZipEntry(archive, note, kMaxXmlEntryBytes, &partTruncated);
        if (partTruncated) {
            if (truncated)
                *truncated = true;
            continue;
        }
        if (!xml.isEmpty())
            appendBounded(output, xmlParagraphs(xml), truncated);
        if (output.size() >= kMaxExtractChars)
            break;
    }
    return output;
}

QString extractOpenDocument(zip_t* archive, bool* truncated)
{
    bool partTruncated = false;
    const QByteArray xml = readZipEntry(archive, QStringLiteral("content.xml"),
                                        kMaxXmlEntryBytes, &partTruncated);
    if (partTruncated) {
        if (truncated)
            *truncated = true;
        return {};
    }
    if (xml.isEmpty())
        return {};
    bool parseOk = false;
    const QString text = xmlParagraphs(xml, &parseOk);
    if (!parseOk)
        qWarning() << "Could not parse OpenDocument content.xml";
    return text;
}

QString extractVisioXml(zip_t* archive, bool* truncated)
{
    QStringList pages;
    for (const QString& entry : listZipEntries(archive)) {
        if (QRegularExpression(QStringLiteral("^visio/(pages/page\\d+|masters/master\\d+)\\.xml$")
                              ).match(entry).hasMatch()) {
            pages.append(entry);
        }
    }
    sortZipEntries(pages);
    QString output;
    for (const QString& page : pages) {
        bool partTruncated = false;
        const QByteArray xml = readZipEntry(archive, page, kMaxXmlEntryBytes, &partTruncated);
        if (partTruncated) {
            if (truncated)
                *truncated = true;
            continue;
        }
        if (!xml.isEmpty())
            appendBounded(output, xmlNamedText(xml, QStringLiteral("Text")), truncated);
        if (output.size() >= kMaxExtractChars)
            break;
    }
    return output;
}

QString extractXpsXml(zip_t* archive, bool* truncated)
{
    QStringList pages;
    for (const QString& entry : listZipEntries(archive)) {
        if (entry.endsWith(QStringLiteral(".fpage"), Qt::CaseInsensitive))
            pages.append(entry);
    }
    sortZipEntries(pages);
    QString output;
    for (const QString& page : pages) {
        bool partTruncated = false;
        const QByteArray xml = readZipEntry(archive, page, kMaxXmlEntryBytes, &partTruncated);
        if (partTruncated) {
            if (truncated)
                *truncated = true;
            continue;
        }
        if (!xml.isEmpty())
            appendBounded(output, xmlNamedText(xml, QStringLiteral("Glyphs")), truncated);
        if (output.size() >= kMaxExtractChars)
            break;
    }
    return output;
}

QString extractEpub(zip_t* archive, bool* truncated)
{
    QStringList contentEntries;
    for (const QString& entry : listZipEntries(archive)) {
        const QString lower = entry.toLower();
        if (lower.endsWith(QStringLiteral(".xhtml")) || lower.endsWith(QStringLiteral(".html")))
            contentEntries.append(entry);
    }
    sortZipEntries(contentEntries);

    QString output;
    for (const QString& entry : contentEntries) {
        bool partTruncated = false;
        const QByteArray html = readZipEntry(archive, entry, kMaxXmlEntryBytes, &partTruncated);
        if (partTruncated) {
            if (truncated)
                *truncated = true;
            continue;
        }
        if (html.isEmpty())
            continue;
        QTextDocument document;
        document.setHtml(QString::fromUtf8(html));
        appendBounded(output, document.toPlainText() + QLatin1Char('\n'), truncated);
        if (output.size() >= kMaxExtractChars)
            break;
    }
    return output;
}

QString htmlToText(const QByteArray& bytes);

QByteArray decodeQuotedPrintable(const QByteArray& input)
{
    QByteArray output;
    output.reserve(input.size());
    for (qsizetype i = 0; i < input.size(); ++i) {
        if (input[i] == '=' && i + 1 < input.size()) {
            if (input[i + 1] == '\r' && i + 2 < input.size() && input[i + 2] == '\n') {
                i += 2; // soft line break
                continue;
            }
            if (input[i + 1] == '\n') {
                ++i;
                continue;
            }
            if (i + 2 < input.size()) {
                bool ok = false;
                const int value = input.mid(i + 1, 2).toInt(&ok, 16);
                if (ok) {
                    output.append(static_cast<char>(value));
                    i += 2;
                    continue;
                }
            }
        }
        output.append(input[i]);
    }
    return output;
}

QString extractMhtml(const QString& filePath, bool* truncated)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    if (file.size() > kMaxOfficeFileBytes) {
        if (truncated)
            *truncated = true;
        return {};
    }
    const QByteArray source = file.read(kMaxOfficeFileBytes + 1);
    if (source.size() > kMaxOfficeFileBytes) {
        if (truncated)
            *truncated = true;
        return {};
    }

    const QByteArray lower = source.left(64 * 1024).toLower();
    const qsizetype contentTypePos = lower.indexOf("content-type: text/html");
    if (contentTypePos < 0) {
        // Some producers save a single HTML body without a multipart wrapper.
        return htmlToText(source);
    }

    const qsizetype headerEnd = source.indexOf("\r\n\r\n", contentTypePos) >= 0
        ? source.indexOf("\r\n\r\n", contentTypePos)
        : source.indexOf("\n\n", contentTypePos);
    if (headerEnd < 0)
        return {};
    const qsizetype separatorLength = source.mid(headerEnd, 4) == "\r\n\r\n" ? 4 : 2;
    QByteArray headers = source.mid(contentTypePos, headerEnd - contentTypePos).toLower();
    QByteArray body = source.mid(headerEnd + separatorLength);
    const qsizetype nextBoundary = body.indexOf("\r\n--");
    const qsizetype nextBoundaryLf = body.indexOf("\n--");
    qsizetype cutAt = -1;
    if (nextBoundary >= 0)
        cutAt = nextBoundary;
    if (nextBoundaryLf >= 0 && (cutAt < 0 || nextBoundaryLf < cutAt))
        cutAt = nextBoundaryLf;
    if (cutAt >= 0)
        body.truncate(cutAt);

    if (headers.contains("content-transfer-encoding: base64")) {
        body = QByteArray::fromBase64(body);
    } else if (headers.contains("content-transfer-encoding: quoted-printable")) {
        body = decodeQuotedPrintable(body);
    }
    QString html = QString::fromUtf8(body);
    const qsizetype charsetPosition = headers.indexOf("charset=");
    if (charsetPosition >= 0) {
        QByteArray charset = headers.mid(charsetPosition + 8).trimmed();
        const qsizetype semicolon = charset.indexOf(';');
        if (semicolon >= 0)
            charset.truncate(semicolon);
        charset.replace("\"", "");
        charset.replace("'", "");
        charset = charset.trimmed();
        if (charset == "gbk" || charset == "gb2312" || charset == "gb18030"
            || charset == "windows-1252") {
            html = QString::fromLocal8Bit(body);
        }
    }
    const QString text = htmlToText(html.toUtf8());
    if (text.size() > kMaxExtractChars) {
        if (truncated)
            *truncated = true;
        return text.left(static_cast<qsizetype>(kMaxExtractChars));
    }
    return text;
}

QString extractDbfFile(const QString& filePath, bool* truncated)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    if (file.size() < 33 || file.size() > kMaxOfficeFileBytes) {
        if (truncated && file.size() > kMaxOfficeFileBytes)
            *truncated = true;
        return {};
    }

    const QByteArray header = file.read(32);
    if (header.size() != 32)
        return {};
    const auto u8 = [](char value) { return static_cast<quint8>(value); };
    const quint32 recordCount = static_cast<quint32>(u8(header[4]))
        | (static_cast<quint32>(u8(header[5])) << 8)
        | (static_cast<quint32>(u8(header[6])) << 16)
        | (static_cast<quint32>(u8(header[7])) << 24);
    const quint16 headerLength = static_cast<quint16>(u8(header[8]) | (u8(header[9]) << 8));
    const quint16 recordLength = static_cast<quint16>(u8(header[10]) | (u8(header[11]) << 8));
    if (headerLength < 33 || recordLength < 1 || headerLength > file.size())
        return {};

    struct Field { QString name; char type = 0; int length = 0; };
    QVector<Field> fields;
    while (file.pos() + 32 <= headerLength) {
        const QByteArray descriptor = file.read(32);
        if (descriptor.size() != 32)
            return {};
        if (u8(descriptor[0]) == 0x0D)
            break;
        const QByteArray rawName = descriptor.left(11);
        const qsizetype nullAt = rawName.indexOf('\0');
        const QByteArray nameBytes = nullAt >= 0 ? rawName.left(nullAt) : rawName;
        Field field;
        field.name = QString::fromLocal8Bit(nameBytes).trimmed();
        field.type = descriptor[11];
        field.length = u8(descriptor[16]);
        if (field.length > 0)
            fields.append(field);
    }
    if (fields.isEmpty())
        return {};
    if (!file.seek(headerLength))
        return {};

    QString output = QStringLiteral("字段: ");
    QStringList names;
    for (const Field& field : fields)
        names.append(field.name);
    output += names.join(QStringLiteral(" | ")) + QLatin1Char('\n');

    const quint64 maxRecordsFromSize = static_cast<quint64>(file.size() - headerLength)
        / static_cast<quint64>(recordLength);
    const quint32 safeRecordCount = static_cast<quint32>(qMin<quint64>(recordCount, maxRecordsFromSize));
    for (quint32 row = 0; row < safeRecordCount; ++row) {
        const QByteArray record = file.read(recordLength);
        if (record.size() != recordLength)
            break;
        if (record[0] == '*')
            continue;
        qsizetype offset = 1;
        QStringList values;
        for (const Field& field : fields) {
            if (offset + field.length > record.size())
                break;
            const QByteArray raw = record.mid(offset, field.length);
            offset += field.length;
            // Memo and binary fields point to a companion file or contain non-text data.
            if (field.type == 'M' || field.type == 'G' || field.type == 'P' || field.type == 'B')
                continue;
            const QString value = QString::fromLocal8Bit(raw).trimmed();
            if (!value.isEmpty())
                values.append(field.name + QStringLiteral(": ") + value);
        }
        if (!values.isEmpty()) {
            appendBounded(output, values.join(QStringLiteral(" | ")) + QLatin1Char('\n'), truncated);
        }
        if (output.size() >= kMaxExtractChars) {
            if (truncated)
                *truncated = true;
            break;
        }
    }
    if (safeRecordCount < recordCount && truncated)
        *truncated = true;
    return normalizedText(output);
}

QString htmlToText(const QByteArray& bytes)
{
    QTextDocument document;
    document.setHtml(QString::fromUtf8(bytes));
    return normalizedText(document.toPlainText());
}

QString rtfToText(const QByteArray& bytes, bool* truncated)
{
    const QString source = QString::fromLatin1(bytes);
    QString output;
    struct GroupState { bool skip = false; int ucSkip = 1; };
    QVector<GroupState> stack;
    GroupState state;
    int fallbackToSkip = 0;

    const QSet<QString> destinations = {
        QStringLiteral("fonttbl"), QStringLiteral("colortbl"), QStringLiteral("stylesheet"),
        QStringLiteral("info"), QStringLiteral("pict"), QStringLiteral("object"),
        QStringLiteral("header"), QStringLiteral("headerl"), QStringLiteral("headerr"),
        QStringLiteral("footer"), QStringLiteral("footerl"), QStringLiteral("footerr"),
        QStringLiteral("generator"), QStringLiteral("xmlnstbl"), QStringLiteral("listtable"),
        QStringLiteral("listoverridetable"), QStringLiteral("datastore"), QStringLiteral("themedata")
    };

    for (qsizetype i = 0; i < source.size(); ++i) {
        const QChar ch = source.at(i);
        if (ch == QLatin1Char('{')) {
            stack.append(state);
            continue;
        }
        if (ch == QLatin1Char('}')) {
            if (!stack.isEmpty()) {
                state = stack.takeLast();
                fallbackToSkip = 0;
            }
            continue;
        }
        if (state.skip)
            continue;

        if (fallbackToSkip > 0) {
            if (ch == QLatin1Char('\\') && i + 1 < source.size()) {
                if (source.at(i + 1) == QLatin1Char('\''))
                    i = qMin(i + 3, source.size() - 1);
                else
                    ++i;
            }
            --fallbackToSkip;
            continue;
        }

        if (ch != QLatin1Char('\\')) {
            if (ch != QLatin1Char('\n') && ch != QLatin1Char('\r'))
                appendBounded(output, QString(ch), truncated);
            continue;
        }
        if (i + 1 >= source.size())
            break;

        const QChar next = source.at(++i);
        if (next == QLatin1Char('\\') || next == QLatin1Char('{') || next == QLatin1Char('}')) {
            appendBounded(output, QString(next), truncated);
            continue;
        }
        if (next == QLatin1Char('~')) {
            appendBounded(output, QString(QChar(0x00A0)), truncated);
            continue;
        }
        if (next == QLatin1Char('_')) {
            appendBounded(output, QString(QChar(0x2011)), truncated);
            continue;
        }
        if (next == QLatin1Char('-'))
            continue;
        if (next == QLatin1Char('*')) {
            state.skip = true;
            continue;
        }
        if (next == QLatin1Char('\'')) {
            if (i + 2 < source.size()) {
                bool ok = false;
                const int byte = source.mid(i + 1, 2).toInt(&ok, 16);
                if (ok) {
                    const char value = static_cast<char>(byte);
                    appendBounded(output, QString::fromLocal8Bit(&value, 1), truncated);
                    i += 2;
                }
            }
            continue;
        }
        if (!next.isLetter())
            continue;

        const qsizetype wordStart = i;
        while (i + 1 < source.size() && source.at(i + 1).isLetter())
            ++i;
        const QString word = source.mid(wordStart, i - wordStart + 1).toLower();
        bool negative = false;
        if (i + 1 < source.size() && source.at(i + 1) == QLatin1Char('-')) {
            negative = true;
            ++i;
        }
        qsizetype numberStart = i + 1;
        while (numberStart < source.size() && source.at(numberStart).isDigit())
            ++numberStart;
        bool hasNumber = numberStart > i + 1;
        int number = 0;
        if (hasNumber) {
            number = source.mid(i + 1, numberStart - i - 1).toInt();
            if (negative)
                number = -number;
            i = numberStart - 1;
        }
        if (i + 1 < source.size() && source.at(i + 1) == QLatin1Char(' '))
            ++i;

        if (destinations.contains(word)) {
            state.skip = true;
            continue;
        }
        if (word == QStringLiteral("uc") && hasNumber) {
            state.ucSkip = qBound(0, number, 16);
        } else if (word == QStringLiteral("u") && hasNumber) {
            appendBounded(output, QString(QChar(static_cast<ushort>(number & 0xFFFF))), truncated);
            fallbackToSkip = state.ucSkip;
        } else if (word == QStringLiteral("par") || word == QStringLiteral("line")) {
            appendBounded(output, QStringLiteral("\n"), truncated);
        } else if (word == QStringLiteral("tab")) {
            appendBounded(output, QStringLiteral("\t"), truncated);
        } else if (word == QStringLiteral("emdash")) {
            appendBounded(output, QString(QChar(0x2014)), truncated);
        } else if (word == QStringLiteral("endash")) {
            appendBounded(output, QString(QChar(0x2013)), truncated);
        } else if (word == QStringLiteral("bullet")) {
            appendBounded(output, QString(QChar(0x2022)), truncated);
        } else if (word == QStringLiteral("bin") && hasNumber) {
            i = qMin(source.size() - 1, i + number);
        }

        if (output.size() >= kMaxExtractChars) {
            if (truncated)
                *truncated = true;
            break;
        }
    }
    return normalizedText(output);
}

QString powershellLiteral(QString value)
{
    value.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(value);
}

QString legacyOfficePowerShellScript(const QString& filePath, const QString& kind)
{
    const QString pathLiteral = powershellLiteral(QDir::toNativeSeparators(filePath));
    const QString common = QStringLiteral(
        "$ErrorActionPreference='Stop'; "
        "[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false); "
        "$path=%1; $app=$null; $document=$null; $range=$null; $workbooks=$null; $worksheets=$null; "
        "function Release-Com($o) { if ($null -ne $o) { try { [void][System.Runtime.InteropServices.Marshal]::FinalReleaseComObject($o) } catch {} } } ")
        .arg(pathLiteral);

    if (kind == QStringLiteral("word")) {
        return common + QStringLiteral(
            "try { foreach($id in @('Word.Application','KWPS.Application')) { "
            "try { $app=New-Object -ComObject $id; break } catch {} }; "
            "if($null -eq $app){throw 'Word/WPS COM automation is unavailable'}; "
            // Word supports a hidden application window; keep the document read-only and disable automation macros.
            "$app.Visible=$false; $app.DisplayAlerts=0; try{$app.AutomationSecurity=3}catch{}; "
            "try{$app.Options.UpdateLinksAtOpen=$false}catch{}; "
            // Documents.Open(FileName, ConfirmConversions, ReadOnly, AddToRecentFiles).
            "$document=$app.Documents.Open($path,$false,$true,$false); "
            "$range=$document.Content; $text=[string]$range.Text; $limit=8388608; "
            "if($text.Length -gt $limit){[Console]::Write($text.Substring(0,$limit)+[Environment]::NewLine+'__WORKDESK_TRUNCATED__')} "
            "else{[Console]::Write($text)} } "
            "finally { if($range){Release-Com $range}; "
            "if($document){try{$document.Close(0)}catch{}; Release-Com $document}; "
            "if($app){try{$app.Quit(0)}catch{}; Release-Com $app}; "
            "[GC]::Collect(); [GC]::WaitForPendingFinalizers() }");
    }

    if (kind == QStringLiteral("excel")) {
        return common + QStringLiteral(
            "try { foreach($id in @('Excel.Application','KET.Application')) { "
            "try { $app=New-Object -ComObject $id; break } catch {} }; "
            "if($null -eq $app){throw 'Excel/WPS Spreadsheets COM automation is unavailable'}; "
            // Do not show Excel, update external links, run macros or fire workbook events.
            "$app.Visible=$false; $app.DisplayAlerts=$false; $app.AskToUpdateLinks=$false; $app.EnableEvents=$false; "
            "try{$app.AutomationSecurity=3}catch{}; "
            // Workbooks.Open(Filename, UpdateLinks=0, ReadOnly=true).
            "$workbooks=$app.Workbooks; $document=$workbooks.Open($path,0,$true); "
            "$worksheets=$document.Worksheets; $sheetCount=[int]$worksheets.Count; "
            "$builder=New-Object System.Text.StringBuilder; $limit=8388608; "
            "$remainingCells=500000; $maxColumns=512; $didTruncate=$false; "
            "for($sheetIndex=1;$sheetIndex -le $sheetCount -and $builder.Length -lt $limit;$sheetIndex++){ "
            "$sheet=$null; $used=$null; $rowsObject=$null; $columnsObject=$null; $readRange=$null; "
            "if($remainingCells -le 0){$didTruncate=$true; break}; "
            "try { $sheet=$worksheets.Item($sheetIndex); "
            "[void]$builder.AppendLine(('工作表: '+[string]$sheet.Name)); "
            "$used=$sheet.UsedRange; $rowsObject=$used.Rows; $columnsObject=$used.Columns; "
            "$rowCount=[int]$rowsObject.Count; $columnCount=[int]$columnsObject.Count; "
            "$readColumns=[Math]::Max(1,[Math]::Min($columnCount,$maxColumns)); "
            "$readRows=[Math]::Max(1,[Math]::Min($rowCount,[int][Math]::Floor($remainingCells/[double]$readColumns))); "
            "$remainingCells -= ($readRows * $readColumns); "
            "if($readRows -lt $rowCount -or $readColumns -lt $columnCount){$didTruncate=$true}; "
            "$readRange=$used.Resize($readRows,$readColumns); $values=$readRange.Value2; "
            "if($values -is [System.Array]) { "
            "if($values.Rank -ge 2){ "
            "for($r=$values.GetLowerBound(0);$r -le $values.GetUpperBound(0) -and $builder.Length -lt $limit;$r++){ "
            "for($c=$values.GetLowerBound(1);$c -le $values.GetUpperBound(1) -and $builder.Length -lt $limit;$c++){ "
            "$v=$values.GetValue($r,$c); if($null -ne $v -and ![string]::IsNullOrWhiteSpace([string]$v)){[void]$builder.AppendLine([string]$v)} } } "
            "} else { foreach($v in $values){if($builder.Length -ge $limit){break}; if($null -ne $v -and ![string]::IsNullOrWhiteSpace([string]$v)){[void]$builder.AppendLine([string]$v)} } } "
            "} elseif($null -ne $values -and ![string]::IsNullOrWhiteSpace([string]$values)){[void]$builder.AppendLine([string]$values)}; "
            "$values=$null; "
            "} finally { Release-Com $readRange; Release-Com $columnsObject; Release-Com $rowsObject; Release-Com $used; Release-Com $sheet } "
            "}; "
            "if($builder.Length -ge $limit){$didTruncate=$true}; "
            "if($didTruncate){$text=$builder.ToString(); if($text.Length -gt $limit){$text=$text.Substring(0,$limit)}; "
            "[Console]::Write($text+[Environment]::NewLine+'__WORKDESK_TRUNCATED__')} "
            "else{[Console]::Write($builder.ToString())} } "
            "finally { if($worksheets){Release-Com $worksheets}; "
            "if($document){try{$document.Close($false)}catch{}; Release-Com $document}; "
            "if($workbooks){Release-Com $workbooks}; "
            "if($app){try{$app.Quit()}catch{}; Release-Com $app}; "
            "[GC]::Collect(); [GC]::WaitForPendingFinalizers() }");
    }

    if (kind == QStringLiteral("powerpoint")) {
        return common + QStringLiteral(
            "try { foreach($id in @('PowerPoint.Application','KWPP.Application')) { "
            "try { $app=New-Object -ComObject $id; break } catch {} }; "
            "if($null -eq $app){throw 'PowerPoint/WPS Presentation COM automation is unavailable'}; "
            // PowerPoint refuses Application.Visible = false. Open the presentation without a window instead.
            "$app.DisplayAlerts=1; try{$app.AutomationSecurity=3}catch{}; "
            // Presentations.Open(FileName, ReadOnly, Untitled, WithWindow).
            "$document=$app.Presentations.Open($path,$true,$false,$false); "
            "$builder=New-Object System.Text.StringBuilder; $limit=8388608; "
            "foreach($slide in $document.Slides){ if($builder.Length -ge $limit){break}; "
            "[void]$builder.AppendLine(('幻灯片: '+$slide.SlideIndex)); "
            "foreach($shape in $slide.Shapes){ if($builder.Length -ge $limit){break}; "
            "$frame=$null; $range=$null; "
            "try { if([int]$shape.HasTextFrame -ne 0){ $frame=$shape.TextFrame; "
            "if([int]$frame.HasText -ne 0){ $range=$frame.TextRange; $shapeText=[string]$range.Text; "
            "if(-not [string]::IsNullOrWhiteSpace($shapeText)){[void]$builder.AppendLine($shapeText)} } } } catch {} "
            "finally { Release-Com $range; Release-Com $frame; Release-Com $shape } }; "
            "Release-Com $slide }; "
            "if($builder.Length -gt $limit){[Console]::Write($builder.ToString(0,$limit)+[Environment]::NewLine+'__WORKDESK_TRUNCATED__')} "
            "else{[Console]::Write($builder.ToString())} } "
            "finally { if($document){try{$document.Close()}catch{}; Release-Com $document}; "
            "if($app){try{$app.Quit()}catch{}; Release-Com $app}; "
            "[GC]::Collect(); [GC]::WaitForPendingFinalizers() }");
    }
    return {};
}

QString extractLegacyOfficeWithCom(const QString& filePath, const QString& kind, bool* truncated,
                                   const std::atomic_bool* cancel)
{
    if (QFileInfo(filePath).size() > kMaxOfficeFileBytes) {
        if (truncated)
            *truncated = true;
        qWarning() << "Legacy Office file exceeds the configured extraction size limit:" << filePath;
        return {};
    }

    const QString script = legacyOfficePowerShellScript(filePath, kind);
    if (script.isEmpty())
        return {};

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(QStringLiteral("powershell.exe"), {
        QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-Command"), script
    });
    const bool started = process.waitForStarted(5000);
    // Office COM startup and large legacy files can be slow; use bounded,
    // application-specific timeouts while still responding to an application shutdown request.
    const int timeoutMs = (kind == QStringLiteral("powerpoint") || kind == QStringLiteral("excel"))
        ? 120000
        : (kind == QStringLiteral("word") ? 90000 : kComTimeoutMs);
    const bool finished = started && waitForProcess(process, timeoutMs, cancel);
    if (!finished && process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(3000);
    }
    if (!started || !finished || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        qWarning() << "Legacy Office COM text extraction failed or timed out:" << filePath
                   << process.readAllStandardError();
        return {};
    }

    QByteArray output = process.readAllStandardOutput();
    const QByteArray truncationMarker("\n__WORKDESK_TRUNCATED__");
    const qsizetype markerPosition = output.indexOf(truncationMarker);
    if (markerPosition >= 0) {
        output.truncate(markerPosition);
        if (truncated)
            *truncated = true;
    }
    if (output.size() > kMaxExtractChars) {
        if (truncated)
            *truncated = true;
        return decodeUtf8Bounded(output, truncated);
    }
    return normalizedText(QString::fromUtf8(output));
}

QString extractPlainTextFile(const QString& filePath, const QString& ext, bool* truncated)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Text extraction failed to open:" << filePath << file.errorString();
        return {};
    }
    const QByteArray data = file.read(kMaxExtractChars + 1);
    if (data.size() > kMaxExtractChars && truncated)
        *truncated = true;

    if (ext == QStringLiteral("rtf"))
        return rtfToText(data, truncated);
    if (ext == QStringLiteral("html") || ext == QStringLiteral("htm")
        || ext == QStringLiteral("xhtml") || ext == QStringLiteral("mht")
        || ext == QStringLiteral("mhtml")) {
        const QString html = htmlToText(data);
        if (html.size() > kMaxExtractChars) {
            if (truncated)
                *truncated = true;
            return html.left(static_cast<qsizetype>(kMaxExtractChars));
        }
        return html;
    }
    return decodeUtf8Bounded(data, truncated);
}

QString extractFlatOpenDocument(const QString& filePath, bool* truncated)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray xml = file.read(kMaxXmlEntryBytes + 1);
    if (xml.size() > kMaxXmlEntryBytes) {
        if (truncated)
            *truncated = true;
        return {};
    }
    bool parseOk = false;
    const QString text = xmlParagraphs(xml, &parseOk);
    if (!parseOk)
        qWarning() << "Could not parse flat OpenDocument XML:" << filePath;
    return text;
}

QString genericOfficeZipExtract(const QString& filePath, const QString& ext, bool* truncated)
{
    if (QFileInfo(filePath).size() > kMaxOfficeFileBytes) {
        if (truncated)
            *truncated = true;
        qWarning() << "Office archive exceeds extraction size limit:" << filePath;
        return {};
    }
    ZipPtr archive = openZip(filePath);
    if (!archive)
        return {};

    if (containsExtension(wordExtensions(), ext)) {
        if (zip_name_locate(archive.get(), "word/document.xml", 0) >= 0)
            return extractWordXml(archive.get(), truncated);
        if (zip_name_locate(archive.get(), "content.xml", 0) >= 0)
            return extractOpenDocument(archive.get(), truncated);
    }
    if (containsExtension(excelExtensions(), ext)) {
        if (zip_name_locate(archive.get(), "xl/workbook.xml", 0) >= 0)
            return extractExcelXml(archive.get(), truncated);
        if (zip_name_locate(archive.get(), "content.xml", 0) >= 0)
            return extractOpenDocument(archive.get(), truncated);
    }
    if (containsExtension(presentationExtensions(), ext)) {
        if (zip_name_locate(archive.get(), "ppt/presentation.xml", 0) >= 0
            || zip_name_locate(archive.get(), "ppt/slides/slide1.xml", 0) >= 0) {
            return extractPowerPointXml(archive.get(), truncated);
        }
        if (zip_name_locate(archive.get(), "content.xml", 0) >= 0)
            return extractOpenDocument(archive.get(), truncated);
    }
    if (containsExtension(diagramExtensions(), ext)) {
        if (zip_name_locate(archive.get(), "content.xml", 0) >= 0)
            return extractOpenDocument(archive.get(), truncated);
        return extractVisioXml(archive.get(), truncated);
    }
    if (ext == QStringLiteral("epub"))
        return extractEpub(archive.get(), truncated);
    if (ext == QStringLiteral("xps") || ext == QStringLiteral("oxps"))
        return extractXpsXml(archive.get(), truncated);
    return {};
}

QString defaultNoTextSummary(const QString& filePath)
{
    const QString ext = extensionOf(filePath);
    if (ext == QStringLiteral("pdf"))
        return QStringLiteral("PDF 文档 · 未提取到可检索文字；可能是扫描件、加密文件或受损文档；扫描件可启用 Tesseract OCR");
    if (containsExtension(imageExtensions(), ext))
        return QStringLiteral("图片文件 · 当前仅收录文件名和基础信息，图像内容检索将在后续版本支持");
    if (ext == QStringLiteral("msg") || ext == QStringLiteral("oft")
        || ext == QStringLiteral("pst") || ext == QStringLiteral("ost")
        || ext == QStringLiteral("olm"))
        return QStringLiteral("邮件数据/模板文件 · 当前仅收录文件名和基础信息");
    if (ext == QStringLiteral("accdb") || ext == QStringLiteral("mdb")
        || ext == QStringLiteral("accde") || ext == QStringLiteral("accdr")
        || ext == QStringLiteral("accdt") || ext == QStringLiteral("accdw")
        || ext == QStringLiteral("mde") || ext == QStringLiteral("wdb"))
        return QStringLiteral("数据库文件 · 当前仅收录文件名和基础信息");
    if (containsExtension(diagramExtensions(), ext))
        return QStringLiteral("设计/CAD 工程文件 · 当前仅收录文件名和基础信息");
    if (containsExtension(otherOfficeExtensions(), ext))
        return QStringLiteral("办公/协作文件 · 当前仅收录文件名和基础信息");
    if (ext == QStringLiteral("xlm") || ext == QStringLiteral("xla")
        || ext == QStringLiteral("xlam") || ext == QStringLiteral("xll")
        || ext == QStringLiteral("xlb"))
        return QStringLiteral("Excel 加载项/宏工作表 · 为避免自动化执行风险，仅收录文件名和基础信息");
    if (ext == QStringLiteral("ppa") || ext == QStringLiteral("ppam"))
        return QStringLiteral("PowerPoint 加载项 · 为避免自动加载插件，仅收录文件名和基础信息");
    return QStringLiteral("未提取到可检索正文 · 文件可能受密码保护、格式暂不支持或本机缺少 Office/WPS");
}
}

QString FileManager::getCategory(const QString& filePath)
{
    const QString ext = extensionOf(filePath);
    if (ext == QStringLiteral("pdf"))
        return QStringLiteral("PDF文档");
    if (ext == QStringLiteral("xps") || ext == QStringLiteral("oxps"))
        return QStringLiteral("XPS 文档");
    if (containsExtension(wordExtensions(), ext)
        || ext == QStringLiteral("abw") || ext == QStringLiteral("pages"))
        return QStringLiteral("Word文档");
    if (containsExtension(excelExtensions(), ext) || ext == QStringLiteral("numbers"))
        return QStringLiteral("Excel表格");
    if (containsExtension(presentationExtensions(), ext) || ext == QStringLiteral("key"))
        return QStringLiteral("演示文稿");
    if (containsExtension(imageExtensions(), ext))
        return QStringLiteral("图片");
    if (containsExtension(diagramExtensions(), ext))
        return QStringLiteral("图形与设计");
    if (containsExtension(emailExtensions(), ext))
        return QStringLiteral("邮件");
    if (ext == QStringLiteral("mht") || ext == QStringLiteral("mhtml"))
        return QStringLiteral("网页归档");
    if (containsExtension(ebookExtensions(), ext))
        return QStringLiteral("电子书");
    if (ext == QStringLiteral("accdb") || ext == QStringLiteral("accde")
        || ext == QStringLiteral("mdb") || ext == QStringLiteral("mde")
        || ext == QStringLiteral("accdr") || ext == QStringLiteral("accdt")
        || ext == QStringLiteral("mda") || ext == QStringLiteral("ade")
        || ext == QStringLiteral("adp") || ext == QStringLiteral("odb")
        || ext == QStringLiteral("accdw") || ext == QStringLiteral("wdb"))
        return QStringLiteral("数据库文件");
    if (containsExtension(otherOfficeExtensions(), ext))
        return QStringLiteral("其他办公文档");
    if (isPlainTextExtension(ext))
        return QStringLiteral("文本代码");
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

bool FileManager::isImageFile(const QString& filePath)
{
    return containsExtension(imageExtensions(), extensionOf(filePath));
}

bool FileManager::isSupportedOfficeFile(const QString& filePath)
{
    const QString ext = extensionOf(filePath);
    return containsExtension(wordExtensions(), ext)
        || containsExtension(excelExtensions(), ext)
        || containsExtension(presentationExtensions(), ext)
        || containsExtension(diagramExtensions(), ext)
        || ext == QStringLiteral("pdf") || ext == QStringLiteral("xps")
        || ext == QStringLiteral("oxps") || ext == QStringLiteral("epub")
        || ext == QStringLiteral("pub") || ext == QStringLiteral("mpp")
        || ext == QStringLiteral("accdb") || ext == QStringLiteral("accde")
        || ext == QStringLiteral("accdr") || ext == QStringLiteral("accdt")
        || ext == QStringLiteral("mdb") || ext == QStringLiteral("mde")
        || ext == QStringLiteral("mda") || ext == QStringLiteral("ade")
        || ext == QStringLiteral("adp") || ext == QStringLiteral("odb")
        || ext == QStringLiteral("one") || ext == QStringLiteral("onepkg")
        || containsExtension(otherOfficeExtensions(), ext)
        || containsExtension(emailExtensions(), ext)
        || ext == QStringLiteral("pst") || ext == QStringLiteral("ost")
        || ext == QStringLiteral("abw") || ext == QStringLiteral("wdb")
        || ext == QStringLiteral("pages") || ext == QStringLiteral("numbers")
        || ext == QStringLiteral("key") || ext == QStringLiteral("mht")
        || ext == QStringLiteral("mhtml") || containsExtension(ebookExtensions(), ext);
}

QString FileManager::extractContent(const QString& filePath, bool* truncated,
                                    const std::atomic_bool* cancel)
{
    if (truncated)
        *truncated = false;

    const QFileInfo fi(filePath);
    const QString ext = fi.suffix().toLower();
    if (!fi.exists() || !fi.isFile())
        return {};
    if (cancel && cancel->load(std::memory_order_relaxed))
        return {};

    // Images are catalogued by path/name/metadata for now; OCR and visual search are a later phase.
    if (containsExtension(imageExtensions(), ext))
        return {};

    if (isPlainTextExtension(ext))
        return extractPlainTextFile(filePath, ext, truncated);

    if (ext == QStringLiteral("abw")) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        const QByteArray xml = file.read(kMaxXmlEntryBytes + 1);
        if (xml.size() > kMaxXmlEntryBytes) {
            if (truncated)
                *truncated = true;
            return {};
        }
        bool parseOk = false;
        const QString text = xmlParagraphs(xml, &parseOk);
        if (!parseOk)
            qWarning() << "Could not parse AbiWord XML:" << filePath;
        return text;
    }

    if (ext == QStringLiteral("mht") || ext == QStringLiteral("mhtml"))
        return extractMhtml(filePath, truncated);

    if (ext == QStringLiteral("fb2")) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        const QByteArray xml = file.read(kMaxXmlEntryBytes + 1);
        if (xml.size() > kMaxXmlEntryBytes) {
            if (truncated)
                *truncated = true;
            return {};
        }
        bool parseOk = false;
        const QString text = xmlParagraphs(xml, &parseOk);
        if (!parseOk)
            qWarning() << "Could not parse FictionBook XML:" << filePath;
        return text;
    }

    if (ext == QStringLiteral("rtf"))
        return extractPlainTextFile(filePath, ext, truncated);

    if (ext == QStringLiteral("pdf"))
        return extractPdfContent(filePath, truncated, cancel);

    if (ext == QStringLiteral("dbf"))
        return extractDbfFile(filePath, truncated);

    if (ext == QStringLiteral("fodt") || ext == QStringLiteral("fods")
        || ext == QStringLiteral("fodp") || ext == QStringLiteral("fodg")) {
        return extractFlatOpenDocument(filePath, truncated);
    }

    if (ext == QStringLiteral("doc") || ext == QStringLiteral("dot")
        || ext == QStringLiteral("wps") || ext == QStringLiteral("wpt")
        || ext == QStringLiteral("wpd") || ext == QStringLiteral("wp")
        || ext == QStringLiteral("lwp") || ext == QStringLiteral("docb"))
        return extractLegacyOfficeWithCom(filePath, QStringLiteral("word"), truncated, cancel);
    if (ext == QStringLiteral("xls") || ext == QStringLiteral("xlt")
        || ext == QStringLiteral("xlsb") || ext == QStringLiteral("xlw")
        || ext == QStringLiteral("et") || ext == QStringLiteral("ett"))
        return extractLegacyOfficeWithCom(filePath, QStringLiteral("excel"), truncated, cancel);
    if (ext == QStringLiteral("ppt") || ext == QStringLiteral("pps")
        || ext == QStringLiteral("pot") || ext == QStringLiteral("dps")
        || ext == QStringLiteral("dpt"))
        return extractLegacyOfficeWithCom(filePath, QStringLiteral("powerpoint"), truncated, cancel);

    if (containsExtension(wordExtensions(), ext)
        || containsExtension(excelExtensions(), ext)
        || containsExtension(presentationExtensions(), ext)
        || containsExtension(diagramExtensions(), ext)
        || ext == QStringLiteral("epub") || ext == QStringLiteral("xps")
        || ext == QStringLiteral("oxps")) {
        return genericOfficeZipExtract(filePath, ext, truncated);
    }

    return {};
}

QString FileManager::makeSummary(const QString& fullText)
{
    if (fullText.isEmpty())
        return {};

    QString preview = fullText.left(kSummaryPreviewChars).trimmed();
    if (fullText.size() > kSummaryPreviewChars && !preview.endsWith(QChar(0x2026)))
        preview.append(QChar(0x2026));
    return preview;
}

QString FileManager::extractSummary(const QString& filePath)
{
    bool ignoredTruncation = false;
    const QString content = extractContent(filePath, &ignoredTruncation);
    return content.isEmpty() ? defaultNoTextSummary(filePath) : makeSummary(content);
}

QString FileManager::noTextSummary(const QString& filePath)
{
    return defaultNoTextSummary(filePath);
}
