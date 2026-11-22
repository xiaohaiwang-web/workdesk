#include "FileListModel.h"

#include <QFileInfo>
#include <algorithm>

FileListModel::FileListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int FileListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_records.size();
}

QVariant FileListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_records.size())
        return {};

    const FileRecord& record = m_records.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        return record.fileName;
    case FilePathRole:
        return record.filePath;
    case SummaryRole:
        return record.summary;
    case CategoryRole:
        return record.category;
    case ExtensionRole:
        return QFileInfo(record.fileName).suffix().toLower();
    case Qt::ToolTipRole:
        return record.summary.isEmpty()
            ? record.filePath
            : record.filePath + QStringLiteral("\n\n") + record.summary;
    default:
        return {};
    }
}

QHash<int, QByteArray> FileListModel::roleNames() const
{
    auto roles = QAbstractListModel::roleNames();
    roles.insert(FilePathRole, "filePath");
    roles.insert(SummaryRole, "summary");
    roles.insert(CategoryRole, "category");
    roles.insert(ExtensionRole, "extension");
    return roles;
}

void FileListModel::setRecords(const QList<FileRecord>& records)
{
    beginResetModel();
    m_defaultRecords = records;
    m_records = records;
    applySort();
    endResetModel();
}

void FileListModel::sortByRecent()
{
    beginResetModel();
    m_sortMode = SortMode::Recent;
    applySort();
    endResetModel();
}

void FileListModel::sortByName(Qt::SortOrder order)
{
    beginResetModel();
    m_sortMode = SortMode::Name;
    m_sortOrder = order;
    applySort();
    endResetModel();
}

void FileListModel::restoreDefaultOrder()
{
    beginResetModel();
    m_sortMode = SortMode::SearchOrder;
    m_records = m_defaultRecords;
    endResetModel();
}

const FileRecord* FileListModel::recordAt(int row) const
{
    if (row < 0 || row >= m_records.size())
        return nullptr;
    return &m_records.at(row);
}

int FileListModel::recordCount() const
{
    return m_records.size();
}

void FileListModel::applySort()
{
    if (m_sortMode == SortMode::SearchOrder)
        return;

    if (m_sortMode == SortMode::Recent) {
        std::stable_sort(m_records.begin(), m_records.end(), [](const FileRecord& left,
                                                               const FileRecord& right) {
            return left.id > right.id;
        });
        return;
    }

    const Qt::SortOrder order = m_sortOrder;
    std::stable_sort(m_records.begin(), m_records.end(), [order](const FileRecord& left,
                                                                 const FileRecord& right) {
        const int comparison = QString::localeAwareCompare(left.fileName, right.fileName);
        return order == Qt::AscendingOrder ? comparison < 0 : comparison > 0;
    });
}
