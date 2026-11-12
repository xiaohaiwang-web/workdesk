#include "workdesk/core/FileManager.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDebug>
#include <QProcess>
#include <string>

// 引入 C 语言库，强制使用 extern "C" 防止 C++ 名称修饰干扰
extern "C" {
    #include <zip.h>
}

// 引入 C++ 库
#include <pugixml.hpp>
#include <aspose/pdf/document.hpp>
#include <aspose/pdf/text_absorber.hpp>

QString FileManager::getCategory(const QString& filePath) {
    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();
    if (ext == "pdf") return "PDF文档";
    if (ext == "doc" || ext == "docx") return "Word文档";
    if (ext == "xls" || ext == "xlsx") return "Excel表格";
    if (ext == "jpg" || ext == "png" || ext == "jpeg" || ext == "gif") return "图片";
    if (ext == "txt" || ext == "md" || ext == "cpp" || ext == "h" || ext == "py" || ext == "json") return "文本代码";
    return "其他";
}

QString FileManager::classifyAndMove(const QString& filePath, const QString& targetDir) {
    QFileInfo fi(filePath);
    QString category = getCategory(filePath);
    
    QDir dir(targetDir);
    if (!dir.exists(category)) dir.mkpath(category);
    
    QString destPath = dir.filePath(category + "/" + fi.fileName());
    
    // 【修复】如果文件已经在目标目录下，直接返回，不再生成带时间戳的副本
    if (QFileInfo(destPath).absoluteFilePath() == fi.absoluteFilePath()) {
        return destPath;
    }

    if (QFile::exists(destPath)) {
        destPath = dir.filePath(category + "/" + fi.completeBaseName() + "_" + 
                   QString::number(QDateTime::currentMSecsSinceEpoch()) + "." + fi.suffix());
    }

    if (QFile::rename(filePath, destPath)) {
        return destPath;
    } else {
        if (QFile::copy(filePath, destPath)) {
            QFile::remove(filePath);
            return destPath;
        }
    }
    return QString();
}

QString FileManager::extractSummary(const QString& filePath) {
    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();
    
    // ================= 1. 纯文本处理 =================
    if (ext == "txt" || ext == "md" || ext == "cpp" || ext == "h" || ext == "py" || ext == "json") {
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            in.setEncoding(QStringConverter::Utf8);
            QString content = in.read(200);
            file.close();
            return content.replace('\n', ' ').replace('\r', ' ');
        }
        return "（文件读取失败）";
    }

    // ================= 2. PDF 提取 (Aspose FOSS) =================
    if (ext == "pdf") {
        try {
            Aspose::Pdf::Document doc(filePath.toStdString());
            Aspose::Pdf::Text::TextAbsorber absorber;
            absorber.Visit(doc);
            std::string fullText = absorber.Text();
            
            QString summary = QString::fromUtf8(fullText.c_str()).simplified();
            return summary.isEmpty() ? "（该PDF为扫描版或无文本层）" : summary.left(200);
        } catch (const std::exception& e) {
            qDebug() << "PDF extract failed:" << e.what();
            return "（PDF提取失败，可能是文件加密或损坏）";
        }
    }

    // ================= 3. 旧版 .doc 提取 (借助系统 Word 转换) =================
    if (ext == "doc") {
        QString tempDocx = QDir::tempPath() + "/" + fi.completeBaseName() + "_tmp.docx";
        QString winPath = QDir::toNativeSeparators(filePath);
        QString winTemp = QDir::toNativeSeparators(tempDocx);

        // 调用 PowerShell 调起 COM 接口静默转换
        QString script = QString(
            "$word = New-Object -ComObject Word.Application; "
            "$word.Visible = $false; "
            "$doc = $word.Documents.Open('%1'); "
            "$doc.SaveAs('%2', 16); "
            "$doc.Close(); "
            "$word.Quit();"
        ).arg(winPath).arg(winTemp);

        QProcess process;
        process.start("powershell", QStringList() << "-NoProfile" << "-Command" << script);
        process.waitForFinished(10000); // 最多等 10 秒

        if (QFile::exists(tempDocx)) {
            QString summary = extractSummary(tempDocx); // 读到内容
            QFile::remove(tempDocx); // 销毁临时文件
            return summary;
        } else {
            return "（.doc旧格式解析失败，请确保已安装Word或WPS）";
        }
    }

    // ================= 4. DOCX 提取 (libzip + pugixml) =================
    if (ext == "docx") {
        int err = 0;
        // libzip 在 Windows 下打开文件路径必须转化为系统本地格式
        zip_t* archive = zip_open(QDir::toNativeSeparators(filePath).toUtf8().constData(), ZIP_RDONLY, &err);
        if (!archive) return "（无法打开DOCX文件）";

        zip_file_t* entry = zip_fopen(archive, "word/document.xml", 0);
        if (!entry) { 
            zip_close(archive); 
            return "（DOCX内部结构异常）"; 
        }

        std::string xmlContent;
        char buf[4096];
        zip_int64_t n;
        while ((n = zip_fread(entry, buf, sizeof(buf))) > 0) {
            xmlContent.append(buf, n);
        }
        zip_fclose(entry);
        zip_close(archive);

        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_string(xmlContent.c_str());
        if (!result) return "（DOCX解析失败）";

        std::string fullText;
        for (pugi::xpath_node node : doc.select_nodes("//*[local-name()='t']")) {
            fullText += node.node().text().get();
            fullText += " ";
        }

        QString summary = QString::fromUtf8(fullText.c_str()).simplified();
        return summary.isEmpty() ? "（DOCX中未提取到文本）" : summary.left(200);
    }

    return "（暂不支持提取此格式的摘要）";
}