#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>

#include "workdesk/core/FileRecord.h"

class FileListModel final : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        FilePathRole = Qt::UserRole,
        SummaryRole,
        CategoryRole,
        ExtensionRole
    };

    explicit FileListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRecords(const QList<FileRecord>& records);
    void sortByRecent();
    void sortByName(Qt::SortOrder order = Qt::AscendingOrder);
    void restoreDefaultOrder();
    const FileRecord* recordAt(int row) const;
    int recordCount() const;

private:
    enum class SortMode { SearchOrder, Recent, Name };
    void applySort();

    QList<FileRecord> m_records;
    QList<FileRecord> m_defaultRecords;
    SortMode m_sortMode = SortMode::SearchOrder;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
};
