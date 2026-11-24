#include "MainWindow.h"
#include "FileListModel.h"
#include "workdesk/core/FileManager.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QFont>
#include <QIcon>
#include <QAction>
#include <QButtonGroup>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QColor blendThemeColor(const QColor& dayColor, double nightWeight)
{
    QColor hsl = dayColor.toHsl();
    const int dayLightness = hsl.lightness();
    int targetLightness = qBound(34, 26 + qRound((255 - dayLightness) * 0.72), 218);
    int targetSaturation = qRound(hsl.saturation() * 0.68);
    if (hsl.saturation() > 135)
        targetLightness = qMax(targetLightness, 108);
    hsl.setHsl(hsl.hue(), targetSaturation, targetLightness, dayColor.alpha());

    const double w = qBound(0.0, nightWeight, 1.0);
    auto channel = [w](int day, int night) {
        return qBound(0, qRound(day + (night - day) * w), 255);
    };
    return QColor(channel(dayColor.red(), hsl.red()),
                  channel(dayColor.green(), hsl.green()),
                  channel(dayColor.blue(), hsl.blue()),
                  channel(dayColor.alpha(), hsl.alpha()));
}

QString adaptStyleSheetToNight(QString styleSheet, double nightWeight)
{
    static const QRegularExpression colorPattern(QStringLiteral("#[0-9A-Fa-f]{6}"));
    QList<QPair<int, int>> spans;
    QList<QString> replacements;
    auto it = colorPattern.globalMatch(styleSheet);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QColor day(match.captured());
        replacements.append(blendThemeColor(day, nightWeight).name(QColor::HexRgb));
        spans.append({match.capturedStart(), match.capturedLength()});
    }
    for (qsizetype i = spans.size() - 1; i >= 0; --i)
        styleSheet.replace(spans.at(i).first, spans.at(i).second, replacements.at(i));

    const QColor dockDay(QStringLiteral("#FFFFFF"));
    const QColor dockNight(QStringLiteral("#252E3A"));
    auto blendSimple = [nightWeight](const QColor& a, const QColor& b) {
        const double w = qBound(0.0, nightWeight, 1.0);
        return QColor(qRound(a.red() + (b.red() - a.red()) * w),
                      qRound(a.green() + (b.green() - a.green()) * w),
                      qRound(a.blue() + (b.blue() - a.blue()) * w));
    };
    styleSheet.replace(QStringLiteral("rgba(255,255,255,245)"),
                       blendSimple(dockDay, dockNight).name(QColor::HexRgb));

    // The global hex-color interpolation also darkens #FFFFFF. Keep primary
    // action labels white so they remain readable on the blue action surface.
    styleSheet += QStringLiteral("\nQPushButton#PrimaryButton { color: #FFFFFF; }\n");
    return styleSheet;
}

QColor fileAccent(const QString& extension)
{
    const QString ext = extension.toLower();
    if (ext == QStringLiteral("pdf") || ext == QStringLiteral("xps") || ext == QStringLiteral("oxps"))
        return QColor(QStringLiteral("#D45C58"));
    if (ext == QStringLiteral("doc") || ext == QStringLiteral("docx")
        || ext == QStringLiteral("docm") || ext == QStringLiteral("dotx")
        || ext == QStringLiteral("dotm") || ext == QStringLiteral("odt")
        || ext == QStringLiteral("ott") || ext == QStringLiteral("rtf"))
        return QColor(QStringLiteral("#3979C9"));
    if (ext == QStringLiteral("xls") || ext == QStringLiteral("xlsx")
        || ext == QStringLiteral("xlsm") || ext == QStringLiteral("xlsb")
        || ext == QStringLiteral("ods") || ext == QStringLiteral("csv"))
        return QColor(QStringLiteral("#32866B"));
    if (ext == QStringLiteral("ppt") || ext == QStringLiteral("pptx")
        || ext == QStringLiteral("pptm") || ext == QStringLiteral("odp"))
        return QColor(QStringLiteral("#C77A42"));
    if (FileManager::isImageFile(QStringLiteral("file.") + ext))
        return QColor(QStringLiteral("#8A72C4"));
    if (ext == QStringLiteral("zip") || ext == QStringLiteral("7z")
        || ext == QStringLiteral("rar"))
        return QColor(QStringLiteral("#B78342"));
    if (ext == QStringLiteral("cpp") || ext == QStringLiteral("c")
        || ext == QStringLiteral("h") || ext == QStringLiteral("hpp")
        || ext == QStringLiteral("py") || ext == QStringLiteral("js")
        || ext == QStringLiteral("ts") || ext == QStringLiteral("json"))
        return QColor(QStringLiteral("#3A8995"));
    return QColor(QStringLiteral("#758499"));
}

class FileItemDelegate final : public QStyledItemDelegate {
public:
    explicit FileItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override
    {
        const bool compact = option.widget && option.widget->property("compactView").toBool();
        if (compact) {
            const int width = option.widget ? qMax(420, option.widget->width() - 24) : 420;
            return QSize(width, 66);
        }
        return QSize(126, 132);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setRenderHint(QPainter::TextAntialiasing, true);

        const bool compact = option.widget && option.widget->property("compactView").toBool();
        const double nightWeight = option.widget
            ? option.widget->property("nightWeight").toDouble() : 0.0;
        const auto themed = [nightWeight](const QString& dayHex) {
            return blendThemeColor(QColor(dayHex), nightWeight);
        };
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
        const QRectF itemRect = QRectF(option.rect).adjusted(2.0, 2.0, -2.0, -2.0);

        // Finder-style items remain visually quiet until hover or selection.
        if (selected || hovered) {
            const QColor fill = selected ? themed(QStringLiteral("#E2ECFC"))
                                         : themed(QStringLiteral("#F1F4F8"));
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            painter->drawRoundedRect(itemRect, 8.0, 8.0);
        }

        const QString fileName = index.data(Qt::DisplayRole).toString();
        const QString extension = index.data(FileListModel::ExtensionRole).toString();
        const QString category = index.data(FileListModel::CategoryRole).toString();
        const QString filePath = index.data(FileListModel::FilePathRole).toString();
        const QColor accent = fileAccent(extension);

        const auto drawDocumentIcon = [painter, accent, extension, themed](const QRectF& rect) {
            QRectF page = rect.adjusted(3.0, 1.0, -4.0, -2.0);
            QPainterPath pagePath;
            pagePath.moveTo(page.left() + 4.0, page.top());
            pagePath.lineTo(page.right() - 12.0, page.top());
            pagePath.lineTo(page.right(), page.top() + 12.0);
            pagePath.lineTo(page.right(), page.bottom() - 4.0);
            pagePath.quadTo(page.right(), page.bottom(), page.right() - 4.0, page.bottom());
            pagePath.lineTo(page.left() + 4.0, page.bottom());
            pagePath.quadTo(page.left(), page.bottom(), page.left(), page.bottom() - 4.0);
            pagePath.lineTo(page.left(), page.top() + 4.0);
            pagePath.quadTo(page.left(), page.top(), page.left() + 4.0, page.top());
            pagePath.closeSubpath();

            painter->setPen(QPen(themed(QStringLiteral("#D5DAE2")), 0.9));
            painter->setBrush(themed(QStringLiteral("#FFFFFF")));
            painter->drawPath(pagePath);

            QPainterPath fold;
            fold.moveTo(page.right() - 12.0, page.top());
            fold.lineTo(page.right() - 12.0, page.top() + 12.0);
            fold.lineTo(page.right(), page.top() + 12.0);
            fold.closeSubpath();
            painter->setPen(Qt::NoPen);
            painter->setBrush(accent.lighter(178));
            painter->drawPath(fold);

            painter->setPen(QPen(accent.lighter(118), 1.6, Qt::SolidLine, Qt::RoundCap));
            const qreal lineLeft = page.left() + 8.0;
            const qreal lineRight = page.right() - 8.0;
            painter->drawLine(QPointF(lineLeft, page.top() + 23.0), QPointF(lineRight, page.top() + 23.0));
            painter->drawLine(QPointF(lineLeft, page.top() + 30.0), QPointF(lineRight - 3.0, page.top() + 30.0));
            painter->drawLine(QPointF(lineLeft, page.top() + 37.0), QPointF(lineRight - 7.0, page.top() + 37.0));

            QFont badgeFont = painter->font();
            badgeFont.setPixelSize(8);
            badgeFont.setWeight(QFont::Bold);
            painter->setFont(badgeFont);
            painter->setPen(accent.darker(130));
            painter->drawText(QRectF(page.left() + 3.0, page.bottom() - 15.0,
                                     page.width() - 6.0, 11.0), Qt::AlignCenter,
                              extension.isEmpty() ? QStringLiteral("FILE")
                                                  : extension.left(4).toUpper());
        };

        const auto drawImageIcon = [painter, accent, themed](const QRectF& rect) {
            QRectF imageRect = rect.adjusted(2.0, 7.0, -2.0, -6.0);
            painter->setPen(QPen(themed(QStringLiteral("#D5DAE2")), 1.0));
            painter->setBrush(themed(QStringLiteral("#FFFFFF")));
            painter->drawRoundedRect(imageRect, 5.0, 5.0);
            painter->save();
            painter->setClipRect(imageRect.adjusted(2.0, 2.0, -2.0, -2.0));
            painter->setPen(Qt::NoPen);
            painter->setBrush(accent.lighter(165));
            painter->drawEllipse(QRectF(imageRect.right() - 20.0, imageRect.top() + 8.0, 9.0, 9.0));
            QPainterPath hills;
            hills.moveTo(imageRect.left() + 2.0, imageRect.bottom() - 4.0);
            hills.lineTo(imageRect.left() + 18.0, imageRect.top() + 22.0);
            hills.lineTo(imageRect.left() + 29.0, imageRect.top() + 33.0);
            hills.lineTo(imageRect.right() - 13.0, imageRect.top() + 18.0);
            hills.lineTo(imageRect.right() - 2.0, imageRect.bottom() - 4.0);
            hills.closeSubpath();
            painter->setBrush(accent.lighter(128));
            painter->drawPath(hills);
            painter->restore();
        };

        if (compact) {
            const QRectF iconRect(itemRect.left() + 12.0, itemRect.center().y() - 23.0, 34.0, 46.0);
            const bool isImage = FileManager::isImageFile(filePath);
            if (isImage)
                drawImageIcon(iconRect);
            else
                drawDocumentIcon(iconRect);

            const qreal textLeft = itemRect.left() + 61.0;
            const qreal textWidth = itemRect.width() - 200.0;
            QFont nameFont = painter->font();
            nameFont.setPixelSize(11);
            nameFont.setWeight(QFont::DemiBold);
            painter->setFont(nameFont);
            painter->setPen(themed(QStringLiteral("#283243")));
            painter->drawText(QRectF(textLeft, itemRect.top() + 10.0, textWidth, 19.0),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              QFontMetrics(nameFont).elidedText(fileName, Qt::ElideRight,
                                                                static_cast<int>(textWidth)));

            QFont detailFont = painter->font();
            detailFont.setPixelSize(9);
            detailFont.setWeight(QFont::Normal);
            painter->setFont(detailFont);
            painter->setPen(themed(QStringLiteral("#8A93A0")));
            painter->drawText(QRectF(textLeft, itemRect.top() + 34.0, textWidth, 15.0),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              QFontMetrics(detailFont).elidedText(QDir::toNativeSeparators(filePath),
                                                                  Qt::ElideMiddle,
                                                                  static_cast<int>(textWidth)));

            const QString meta = category.isEmpty() ? extension.toUpper() : category;
            painter->drawText(QRectF(itemRect.right() - 128.0, itemRect.top() + 22.0, 112.0, 18.0),
                              Qt::AlignRight | Qt::AlignVCenter, meta);
        } else {
            const QRectF iconRect(itemRect.center().x() - 27.0, itemRect.top() + 9.0, 54.0, 68.0);
            const bool isImage = FileManager::isImageFile(filePath);
            if (isImage)
                drawImageIcon(iconRect);
            else
                drawDocumentIcon(iconRect);

            QFont nameFont = painter->font();
            nameFont.setPixelSize(10);
            nameFont.setWeight(QFont::Medium);
            painter->setFont(nameFont);
            painter->setPen(themed(QStringLiteral("#303A49")));
            const QRect nameRect(static_cast<int>(itemRect.left() + 5.0),
                                 static_cast<int>(itemRect.top() + 82.0),
                                 static_cast<int>(itemRect.width() - 10.0), 31);
            painter->drawText(nameRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                              QFontMetrics(nameFont).elidedText(fileName, Qt::ElideRight, 106));

            QFont metaFont = painter->font();
            metaFont.setPixelSize(8);
            metaFont.setWeight(QFont::Normal);
            painter->setFont(metaFont);
            painter->setPen(themed(QStringLiteral("#929AA6")));
            const QString meta = category.isEmpty() ? extension.toUpper() : category;
            painter->drawText(QRectF(itemRect.left() + 4.0, itemRect.bottom() - 17.0,
                                     itemRect.width() - 8.0, 12.0),
                              Qt::AlignHCenter | Qt::AlignVCenter,
                              QFontMetrics(metaFont).elidedText(meta, Qt::ElideRight, 110));
        }

        painter->restore();
    }
};

QIcon dockGlyph(const QString& glyph, const QColor& color)
{
    QPixmap pixmap(44, 44);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QFont font(QStringLiteral("Segoe UI Symbol"));
    font.setPixelSize(27);
    font.setWeight(QFont::Normal);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, glyph);
    return QIcon(pixmap);
}
}

QString MainWindow::themeStyleSheet(double nightWeight)
{
    const QString dayStyleSheet = QStringLiteral(R"(
        QMainWindow, QWidget {
            font-family: "Segoe UI Variable", "Segoe UI", "Microsoft YaHei UI", sans-serif;
            color: #303846;
            font-size: 12px;
        }
        QMainWindow { background: #D8DCE2; }
        QWidget#WindowSurface { background: #D8DCE2; }
        QFrame#SystemBar { background: #FAFAFB; border-bottom: 1px solid #E4E6EA; }
        QToolButton#WindowClose, QToolButton#WindowMinimize, QToolButton#WindowZoom {
            border: 0; border-radius: 7px; min-width: 13px; max-width: 13px;
            min-height: 13px; max-height: 13px; padding: 0;
        }
        QToolButton#WindowClose { background: #FF6058; }
        QToolButton#WindowMinimize { background: #FFBD2E; }
        QToolButton#WindowZoom { background: #28C840; }
        QToolButton#WindowClose:hover { background: #F34F48; }
        QToolButton#WindowMinimize:hover { background: #EAA51A; }
        QToolButton#WindowZoom:hover { background: #1EAF35; }
        QLabel#BrandName { color: #28313D; font-size: 12px; font-weight: 700; }
        QLabel#SystemCaption { color: #A0A5AE; font-size: 9px; }
        QToolButton#MenuButton {
            color: #656D79; background: transparent; border: 0;
            padding: 5px 8px; border-radius: 5px; font-size: 10px;
        }
        QToolButton#MenuButton:hover { color: #28313D; background: #ECEEF1; }
        QLabel#EngineStatus {
            color: #557A68; background: #EEF5F0; border: 1px solid #DCE9E0;
            border-radius: 10px; padding: 4px 9px; font-size: 9px; font-weight: 600;
        }
        QLabel#EngineStatus[unavailable="true"] {
            color: #A65A54; background: #FFF1EF; border: 1px solid #F2D8D4;
        }
        QFrame#SystemBarSeparator { color: #E3E6EA; }
        QFrame#SidebarSeparator {
            color: #E3E5E9; background: #E3E5E9; max-height: 1px;
        }
        QFrame#Sidebar { background: #F7F7F9; border-right: 1px solid #E3E5E9; }
        QLabel#SidebarCaption {
            color: #989EA8; font-size: 9px; font-weight: 700;
            padding: 9px 9px 4px 9px;
        }
        QPushButton#NavigationButton {
            text-align: left; border: 0; border-radius: 6px;
            background: transparent; color: #636C79;
            padding: 7px 10px; min-height: 19px; font-size: 10px;
        }
        QPushButton#NavigationButton:hover { color: #303846; background: #ECEEF2; }
        QPushButton#NavigationButton:checked {
            color: #2E5E9B; background: #E4ECF8; font-weight: 650;
        }
        QLabel#SidebarFootTitle { color: #9AA1AB; font-size: 9px; font-weight: 650; }
        QLabel#SidebarFootText { color: #707A88; font-size: 9px; }
        QLabel#ThemeIndicator {
            color: #9A783F; background: transparent; font-size: 9px;
            padding: 3px 0;
        }
        QFrame#MainArea { background: #F2F3F5; }
        QLabel#PageTitle { color: #272F3B; font-size: 16px; font-weight: 680; }
        QLabel#PageSubtitle { color: #9299A4; font-size: 9px; }
        QLabel#SectionTitle { color: #303846; font-size: 12px; font-weight: 680; }
        QLabel#MutedText { color: #9199A4; font-size: 9px; }
        QFrame#SearchBar { background: #FFFFFF; border: 1px solid #DDE1E7; border-radius: 8px; }
        QFrame#SearchBar:focus-within { border: 1px solid #AABBD4; }
        QLabel#SearchGlyph { color: #778394; font-size: 17px; }
        QLineEdit#SearchInput {
            background: transparent; border: 0; color: #303846;
            padding: 0 4px; font-size: 11px; selection-background-color: #D7E4F6;
        }
        QLineEdit#SearchInput::placeholder { color: #A2A8B1; }
        QLabel#ShortcutHint {
            color: #9299A4; background: #F5F6F8; border: 1px solid #E7E9ED;
            border-radius: 4px; padding: 3px 6px; font-size: 8px;
        }
        QFrame#BrowserCanvas { background: #FFFFFF; border: 1px solid #E2E5EA; border-radius: 10px; }
        QFrame#InspectorPanel { background: #FAFAFB; border: 1px solid #E2E5EA; border-radius: 10px; }
        QLabel#InspectorCaption { color: #989EA8; font-size: 9px; font-weight: 700; }
        QLabel#DetailName { color: #303846; font-size: 12px; font-weight: 680; }
        QLabel#DetailCategory {
            color: #5575A5; background: #EEF2F8; border: 1px solid #E0E7F1;
            border-radius: 5px; padding: 3px 6px; font-size: 8px;
        }
        QLabel#DetailExtension {
            color: #5677A7; background: #EEF2F8; border: 1px solid #E0E7F1;
            border-radius: 8px; font-size: 11px; font-weight: 700;
        }
        QLabel#DetailPath { color: #7E8793; font-size: 9px; }
        QLabel#DetailMeta { color: #9299A4; font-size: 8px; }
        QLabel#DetailSummary { color: #5E6877; font-size: 10px; }
        QLabel#EmptyGlyph { color: #A1AAB7; font-size: 31px; }
        QLabel#EmptyTitle { color: #3C4654; font-size: 13px; font-weight: 650; }
        QLabel#EmptyDescription { color: #929AA5; font-size: 9px; }
        QPushButton#PrimaryButton {
            color: #FFFFFF; background: #4F78AE; border: 1px solid #4A72A7;
            border-radius: 7px; padding: 7px 12px; font-size: 10px; font-weight: 650;
        }
        QPushButton#PrimaryButton:hover { background: #446D9F; }
        QPushButton#PrimaryButton:pressed { background: #3C608D; }
        QComboBox#SortBox {
            color: #697382; background: #FFFFFF; border: 1px solid #E1E4E9;
            border-radius: 6px; padding: 5px 23px 5px 8px; font-size: 9px;
        }
        QComboBox#SortBox::drop-down { border: 0; width: 18px; }
        QToolButton#ViewButton {
            color: #7C8591; background: transparent; border: 1px solid transparent;
            border-radius: 5px; min-width: 27px; min-height: 26px; font-size: 13px;
        }
        QToolButton#ViewButton:checked {
            color: #466A9B; background: #E9EFF8; border: 1px solid #DDE6F2;
        }
        QListView#FileView { background: transparent; border: 0; outline: 0; padding: 8px; }
        QFrame#DockPanel { background: rgba(255,255,255,245); border: 1px solid #DFE3E9; border-radius: 13px; }
        QToolButton#DockButton {
            color: #697382; background: transparent; border: 0;
            border-radius: 8px; min-width: 58px; min-height: 58px;
            padding: 3px 5px; font-size: 8px;
        }
        QToolButton#DockButton:hover { color: #3B5F91; background: #EDF1F7; }
        QLabel#StatusLabel { color: #9098A3; font-size: 8px; }
        QMenu {
            background: #FFFFFF; color: #3C4654; border: 1px solid #E1E5EB;
            border-radius: 7px; padding: 4px;
        }
        QMenu::item { padding: 6px 23px 6px 11px; border-radius: 4px; }
        QMenu::item:selected { color: #315D94; background: #EAF0F8; }
        QToolTip { color: #3C4654; background: #FFFFFF; border: 1px solid #DDE2E9; padding: 4px 6px; }
        QScrollBar:vertical { background: transparent; width: 7px; margin: 3px 1px; }
        QScrollBar::handle:vertical { background: #D6DAE1; min-height: 24px; border-radius: 3px; }
        QScrollBar::handle:vertical:hover { background: #BDC5D0; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
    )");
    return adaptStyleSheetToNight(dayStyleSheet, nightWeight);
}

QWidget* MainWindow::createSystemBar()
{
    auto* bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("SystemBar"));
    bar->setFixedHeight(43);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(14, 0, 16, 0);
    layout->setSpacing(8);

    auto makeWindowControl = [bar, layout](const QString& objectName, const QString& tooltip) {
        auto* control = new QToolButton(bar);
        control->setObjectName(objectName);
        control->setToolTip(tooltip);
        control->setCursor(Qt::PointingHandCursor);
        control->setFixedSize(13, 13);
        layout->addWidget(control);
        return control;
    };

    auto* closeControl = makeWindowControl(QStringLiteral("WindowClose"), QStringLiteral("关闭窗口"));
    auto* minimizeControl = makeWindowControl(QStringLiteral("WindowMinimize"), QStringLiteral("最小化"));
    auto* zoomControl = makeWindowControl(QStringLiteral("WindowZoom"), QStringLiteral("最大化 / 还原"));
    connect(closeControl, &QToolButton::clicked, this, &MainWindow::close);
    connect(minimizeControl, &QToolButton::clicked, this, &MainWindow::showMinimized);
    connect(zoomControl, &QToolButton::clicked, this, [this]() {
        isMaximized() ? showNormal() : showMaximized();
    });

    layout->addSpacing(8);
    auto* brandName = new QLabel(QStringLiteral("WorkDesk"), bar);
    brandName->setObjectName(QStringLiteral("BrandName"));
    layout->addWidget(brandName);

    auto* separator = new QFrame(bar);
    separator->setObjectName(QStringLiteral("SystemBarSeparator"));
    separator->setFrameShape(QFrame::VLine);
    separator->setFixedHeight(16);
    layout->addWidget(separator);

    auto* fileMenuButton = new QToolButton(bar);
    fileMenuButton->setObjectName(QStringLiteral("MenuButton"));
    fileMenuButton->setText(QStringLiteral("文件"));
    fileMenuButton->setPopupMode(QToolButton::InstantPopup);
    auto* fileMenu = new QMenu(fileMenuButton);
    QAction* importAction = fileMenu->addAction(QStringLiteral("添加文件…"));
    connect(importAction, &QAction::triggered, this, &MainWindow::chooseFiles);
    QAction* refreshAction = fileMenu->addAction(QStringLiteral("刷新文件库"));
    connect(refreshAction, &QAction::triggered, this, &MainWindow::runSearch);
    fileMenu->addSeparator();
    QAction* exitAction = fileMenu->addAction(QStringLiteral("退出 WorkDesk"));
    connect(exitAction, &QAction::triggered, this, &MainWindow::requestShutdown);
    fileMenuButton->setMenu(fileMenu);
    layout->addWidget(fileMenuButton);

    auto* viewMenuButton = new QToolButton(bar);
    viewMenuButton->setObjectName(QStringLiteral("MenuButton"));
    viewMenuButton->setText(QStringLiteral("显示"));
    viewMenuButton->setPopupMode(QToolButton::InstantPopup);
    auto* viewMenu = new QMenu(viewMenuButton);
    QAction* gridAction = viewMenu->addAction(QStringLiteral("图标视图"));
    QAction* listAction = viewMenu->addAction(QStringLiteral("列表视图"));
    connect(gridAction, &QAction::triggered, this, [this]() { setViewMode(false); });
    connect(listAction, &QAction::triggered, this, [this]() { setViewMode(true); });
    viewMenuButton->setMenu(viewMenu);
    layout->addWidget(viewMenuButton);

    layout->addStretch();
    m_engineStatusLabel = new QLabel(QStringLiteral("●  本地文件库正在连接"), bar);
    m_engineStatusLabel->setObjectName(QStringLiteral("EngineStatus"));
    layout->addWidget(m_engineStatusLabel);
    return bar;
}

QWidget* MainWindow::createSidebar()
{
    auto* sidebar = new QFrame(this);
    sidebar->setObjectName(QStringLiteral("Sidebar"));
    sidebar->setFixedWidth(204);
    auto* layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(12, 14, 12, 13);
    layout->setSpacing(3);

    auto* favoritesCaption = new QLabel(QStringLiteral("个人收藏"), sidebar);
    favoritesCaption->setObjectName(QStringLiteral("SidebarCaption"));
    layout->addWidget(favoritesCaption);

    auto addNavigation = [this, layout](const QString& label, const QString& category) {
        QPushButton* button = createNavigationButton(label, category);
        layout->addWidget(button);
        m_navigationButtons.insert(category, button);
        return button;
    };

    QPushButton* allButton = addNavigation(QStringLiteral("▦   全部文件"), QString());
    allButton->setChecked(true);
    addNavigation(QStringLiteral("◷   最近项目"), QStringLiteral("__recent__"));

    layout->addSpacing(13);
    auto* locationsCaption = new QLabel(QStringLiteral("文件类型"), sidebar);
    locationsCaption->setObjectName(QStringLiteral("SidebarCaption"));
    layout->addWidget(locationsCaption);
    addNavigation(QStringLiteral("▤   PDF 文稿"), QStringLiteral("PDF文档"));
    addNavigation(QStringLiteral("▤   Word 文稿"), QStringLiteral("Word文档"));
    addNavigation(QStringLiteral("▦   Excel 表格"), QStringLiteral("Excel表格"));
    addNavigation(QStringLiteral("▧   演示文稿"), QStringLiteral("演示文稿"));
    addNavigation(QStringLiteral("▧   图片"), QStringLiteral("图片"));
    addNavigation(QStringLiteral("✉   邮件"), QStringLiteral("邮件"));
    addNavigation(QStringLiteral("◇   图形与设计"), QStringLiteral("图形与设计"));
    addNavigation(QStringLiteral("▧   电子书"), QStringLiteral("电子书"));
    addNavigation(QStringLiteral("▦   数据库文件"), QStringLiteral("数据库文件"));
    addNavigation(QStringLiteral("▤   其他办公文档"), QStringLiteral("其他办公文档"));
    addNavigation(QStringLiteral("▱   网页归档"), QStringLiteral("网页归档"));
    addNavigation(QStringLiteral("⌘   文本与代码"), QStringLiteral("文本代码"));
    addNavigation(QStringLiteral("⋯   其他文件"), QStringLiteral("其他"));

    layout->addStretch();
    auto* rule = new QFrame(sidebar);
    rule->setObjectName(QStringLiteral("SidebarSeparator"));
    rule->setFrameShape(QFrame::HLine);
    layout->addWidget(rule);
    layout->addSpacing(8);
    auto* deviceTitle = new QLabel(QStringLiteral("本地位置"), sidebar);
    deviceTitle->setObjectName(QStringLiteral("SidebarFootTitle"));
    auto* deviceValue = new QLabel(QStringLiteral("●  此设备 · WorkDesk Library"), sidebar);
    deviceValue->setObjectName(QStringLiteral("SidebarFootText"));
    layout->addWidget(deviceTitle);
    layout->addWidget(deviceValue);
    m_themeIndicatorLabel = new QLabel(QStringLiteral("☀  日间 · 自动"), sidebar);
    m_themeIndicatorLabel->setObjectName(QStringLiteral("ThemeIndicator"));
    layout->addSpacing(5);
    layout->addWidget(m_themeIndicatorLabel);
    return sidebar;
}

QPushButton* MainWindow::createNavigationButton(const QString& label, const QString& category)
{
    auto* button = new QPushButton(label, this);
    button->setObjectName(QStringLiteral("NavigationButton"));
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(32);
    connect(button, &QPushButton::clicked, this, [this, category, label]() {
        const int separator = label.indexOf(QStringLiteral("  "));
        const QString displayLabel = separator >= 0 ? label.mid(separator).trimmed() : label;
        setCategoryFilter(category, displayLabel);
    });
    return button;
}

QWidget* MainWindow::createMainContent()
{
    auto* area = new QFrame(this);
    area->setObjectName(QStringLiteral("MainArea"));
    auto* layout = new QVBoxLayout(area);
    layout->setContentsMargins(20, 14, 20, 10);
    layout->setSpacing(10);

    // Finder-like toolbar: location, search and view controls share one compact row.
    auto* toolbar = new QHBoxLayout();
    toolbar->setSpacing(10);
    auto* headingText = new QVBoxLayout();
    headingText->setSpacing(2);
    m_listTitleLabel = new QLabel(QStringLiteral("全部文件"), area);
    m_listTitleLabel->setObjectName(QStringLiteral("PageTitle"));
    m_resultCountLabel = new QLabel(QStringLiteral("正在连接本地文件库…"), area);
    m_resultCountLabel->setObjectName(QStringLiteral("PageSubtitle"));
    headingText->addWidget(m_listTitleLabel);
    headingText->addWidget(m_resultCountLabel);
    toolbar->addLayout(headingText);
    toolbar->addSpacing(8);

    auto* searchBar = new QFrame(area);
    searchBar->setObjectName(QStringLiteral("SearchBar"));
    searchBar->setFixedHeight(38);
    searchBar->setMinimumWidth(190);
    auto* searchLayout = new QHBoxLayout(searchBar);
    searchLayout->setContentsMargins(10, 0, 9, 0);
    searchLayout->setSpacing(8);
    auto* searchGlyph = new QLabel(QStringLiteral("⌕"), searchBar);
    searchGlyph->setObjectName(QStringLiteral("SearchGlyph"));
    auto* searchEdit = new QLineEdit(searchBar);
    searchEdit->setObjectName(QStringLiteral("SearchInput"));
    searchEdit->setPlaceholderText(QStringLiteral("搜索此设备上的文件…"));
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setMinimumWidth(100);
    m_searchEdit = searchEdit;
    auto* shortcut = new QLabel(QStringLiteral("Ctrl K"), searchBar);
    shortcut->setObjectName(QStringLiteral("ShortcutHint"));
    searchLayout->addWidget(searchGlyph);
    searchLayout->addWidget(searchEdit, 1);
    searchLayout->addWidget(shortcut);
    toolbar->addWidget(searchBar, 1);

    auto* addButton = new QPushButton(QStringLiteral("＋ 添加"), area);
    addButton->setObjectName(QStringLiteral("PrimaryButton"));
    addButton->setCursor(Qt::PointingHandCursor);
    connect(addButton, &QPushButton::clicked, this, &MainWindow::chooseFiles);
    toolbar->addWidget(addButton);

    auto* sortBox = new QComboBox(area);
    sortBox->setObjectName(QStringLiteral("SortBox"));
    sortBox->addItem(QStringLiteral("默认排序"), 0);
    sortBox->addItem(QStringLiteral("最近添加"), 1);
    sortBox->addItem(QStringLiteral("名称 A–Z"), 2);
    sortBox->addItem(QStringLiteral("名称 Z–A"), 3);
    sortBox->setMinimumWidth(98);
    connect(sortBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (!m_fileModel)
            return;
        if (index == 0)
            m_fileModel->restoreDefaultOrder();
        else if (index == 1)
            m_fileModel->sortByRecent();
        else if (index == 2)
            m_fileModel->sortByName(Qt::AscendingOrder);
        else
            m_fileModel->sortByName(Qt::DescendingOrder);
    });
    toolbar->addWidget(sortBox);

    auto* viewGroup = new QButtonGroup(area);
    viewGroup->setExclusive(true);
    m_gridModeButton = new QToolButton(area);
    m_gridModeButton->setObjectName(QStringLiteral("ViewButton"));
    m_gridModeButton->setText(QStringLiteral("▦"));
    m_gridModeButton->setToolTip(QStringLiteral("图标视图"));
    m_gridModeButton->setCheckable(true);
    m_listModeButton = new QToolButton(area);
    m_listModeButton->setObjectName(QStringLiteral("ViewButton"));
    m_listModeButton->setText(QStringLiteral("☷"));
    m_listModeButton->setToolTip(QStringLiteral("列表视图"));
    m_listModeButton->setCheckable(true);
    viewGroup->addButton(m_gridModeButton);
    viewGroup->addButton(m_listModeButton);
    connect(m_gridModeButton, &QToolButton::clicked, this, [this]() { setViewMode(false); });
    connect(m_listModeButton, &QToolButton::clicked, this, [this]() { setViewMode(true); });
    toolbar->addWidget(m_gridModeButton);
    toolbar->addWidget(m_listModeButton);
    layout->addLayout(toolbar);

    auto* contentRow = new QHBoxLayout();
    contentRow->setSpacing(10);
    auto* browser = new QFrame(area);
    browser->setObjectName(QStringLiteral("BrowserCanvas"));
    auto* browserLayout = new QVBoxLayout(browser);
    browserLayout->setContentsMargins(8, 8, 8, 8);
    browserLayout->setSpacing(3);

    m_contentStack = new QStackedWidget(browser);
    m_fileModel = new FileListModel(this);
    m_proxyModel = new QSortFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_fileModel);
    m_proxyModel->setFilterKeyColumn(0);
    m_proxyModel->setFilterRole(FileListModel::CategoryRole);
    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel->setDynamicSortFilter(false);

    m_fileView = new QListView(m_contentStack);
    m_fileView->setObjectName(QStringLiteral("FileView"));
    m_fileView->setModel(m_proxyModel);
    m_fileView->setItemDelegate(new FileItemDelegate(m_fileView));
    m_fileView->setViewMode(QListView::IconMode);
    m_fileView->setFlow(QListView::LeftToRight);
    m_fileView->setResizeMode(QListView::Adjust);
    m_fileView->setMovement(QListView::Static);
    m_fileView->setWrapping(true);
    m_fileView->setGridSize(QSize(126, 132));
    m_fileView->setSpacing(2);
    m_fileView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fileView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fileView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_fileView->setMouseTracking(true);
    m_fileView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_fileView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_fileView->setProperty("compactView", false);
    m_contentStack->addWidget(m_fileView);

    auto* emptySurface = new QWidget(m_contentStack);
    auto* emptyLayout = new QVBoxLayout(emptySurface);
    emptyLayout->setContentsMargins(20, 30, 20, 30);
    emptyLayout->setAlignment(Qt::AlignCenter);
    auto* emptyGlyph = new QLabel(QStringLiteral("▧"), emptySurface);
    emptyGlyph->setObjectName(QStringLiteral("EmptyGlyph"));
    emptyGlyph->setAlignment(Qt::AlignCenter);
    m_emptyTitleLabel = new QLabel(QStringLiteral("这里还没有文件"), emptySurface);
    m_emptyTitleLabel->setObjectName(QStringLiteral("EmptyTitle"));
    m_emptyTitleLabel->setAlignment(Qt::AlignCenter);
    m_emptyDescriptionLabel = new QLabel(
        QStringLiteral("将文件拖到 WorkDesk，或点击“添加”导入。\n文件会在本地整理，不需要上传到云端。"), emptySurface);
    m_emptyDescriptionLabel->setObjectName(QStringLiteral("EmptyDescription"));
    m_emptyDescriptionLabel->setAlignment(Qt::AlignCenter);
    m_emptyDescriptionLabel->setWordWrap(true);
    emptyLayout->addStretch();
    emptyLayout->addWidget(emptyGlyph);
    emptyLayout->addSpacing(7);
    emptyLayout->addWidget(m_emptyTitleLabel);
    emptyLayout->addSpacing(4);
    emptyLayout->addWidget(m_emptyDescriptionLabel);
    emptyLayout->addStretch();
    m_contentStack->addWidget(emptySurface);
    browserLayout->addWidget(m_contentStack, 1);

    auto* browserFooter = new QHBoxLayout();
    auto* hint = new QLabel(QStringLiteral("双击打开 · 右键显示操作 · 拖放文件以归档"), browser);
    hint->setObjectName(QStringLiteral("MutedText"));
    browserFooter->addWidget(hint);
    browserFooter->addStretch();
    m_fileCountLabel = new QLabel(QStringLiteral("0 项"), browser);
    m_fileCountLabel->setObjectName(QStringLiteral("MutedText"));
    browserFooter->addWidget(m_fileCountLabel);
    browserLayout->addLayout(browserFooter);
    contentRow->addWidget(browser, 1);

    auto* inspector = new QFrame(area);
    inspector->setObjectName(QStringLiteral("InspectorPanel"));
    inspector->setFixedWidth(252);
    auto* inspectorLayout = new QVBoxLayout(inspector);
    inspectorLayout->setContentsMargins(14, 13, 14, 13);
    inspectorLayout->setSpacing(8);

    auto* inspectorTop = new QHBoxLayout();
    auto* inspectorCaption = new QLabel(QStringLiteral("检查器"), inspector);
    inspectorCaption->setObjectName(QStringLiteral("InspectorCaption"));
    inspectorTop->addWidget(inspectorCaption);
    inspectorTop->addStretch();
    auto* localBadge = new QLabel(QStringLiteral("本地"), inspector);
    localBadge->setObjectName(QStringLiteral("DetailCategory"));
    inspectorTop->addWidget(localBadge);
    inspectorLayout->addLayout(inspectorTop);

    auto* identity = new QHBoxLayout();
    identity->setSpacing(9);
    m_detailExtensionLabel = new QLabel(QStringLiteral("—"), inspector);
    m_detailExtensionLabel->setObjectName(QStringLiteral("DetailExtension"));
    m_detailExtensionLabel->setFixedSize(43, 48);
    m_detailExtensionLabel->setAlignment(Qt::AlignCenter);
    auto* identityText = new QVBoxLayout();
    identityText->setSpacing(4);
    m_detailNameLabel = new QLabel(QStringLiteral("尚未选择文件"), inspector);
    m_detailNameLabel->setObjectName(QStringLiteral("DetailName"));
    m_detailNameLabel->setWordWrap(true);
    m_detailCategoryLabel = new QLabel(QStringLiteral("等待选择"), inspector);
    m_detailCategoryLabel->setObjectName(QStringLiteral("DetailCategory"));
    m_detailCategoryLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    identityText->addWidget(m_detailNameLabel);
    identityText->addWidget(m_detailCategoryLabel, 0, Qt::AlignLeft);
    identity->addWidget(m_detailExtensionLabel);
    identity->addLayout(identityText, 1);
    inspectorLayout->addLayout(identity);

    m_detailMetaLabel = new QLabel(QStringLiteral("此设备上的文件项目"), inspector);
    m_detailMetaLabel->setObjectName(QStringLiteral("DetailMeta"));
    m_detailMetaLabel->setWordWrap(true);
    inspectorLayout->addWidget(m_detailMetaLabel);

    auto* pathCaption = new QLabel(QStringLiteral("位置"), inspector);
    pathCaption->setObjectName(QStringLiteral("InspectorCaption"));
    inspectorLayout->addWidget(pathCaption);
    m_detailPathLabel = new QLabel(QStringLiteral("选择文件后显示它在此设备上的位置。"), inspector);
    m_detailPathLabel->setObjectName(QStringLiteral("DetailPath"));
    m_detailPathLabel->setWordWrap(true);
    m_detailPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    inspectorLayout->addWidget(m_detailPathLabel);

    auto* summaryCaption = new QLabel(QStringLiteral("摘要"), inspector);
    summaryCaption->setObjectName(QStringLiteral("InspectorCaption"));
    inspectorLayout->addSpacing(5);
    inspectorLayout->addWidget(summaryCaption);
    m_detailSummaryLabel = new QLabel(
        QStringLiteral("选择一个文件，在这里查看 WorkDesk 已提取的内容摘要。"), inspector);
    m_detailSummaryLabel->setObjectName(QStringLiteral("DetailSummary"));
    m_detailSummaryLabel->setWordWrap(true);
    m_detailSummaryLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    inspectorLayout->addWidget(m_detailSummaryLabel, 1);

    m_openSelectedButton = new QPushButton(QStringLiteral("打开文件  ↗"), inspector);
    m_openSelectedButton->setObjectName(QStringLiteral("PrimaryButton"));
    m_openSelectedButton->setEnabled(false);
    connect(m_openSelectedButton, &QPushButton::clicked, this, [this]() {
        if (m_fileView)
            onFileActivated(m_fileView->currentIndex());
    });
    inspectorLayout->addWidget(m_openSelectedButton);
    contentRow->addWidget(inspector);
    layout->addLayout(contentRow, 1);

    // The dock is an action launcher, not a decorative strip: each item is wired.
    auto* dockRow = new QHBoxLayout();
    dockRow->addStretch();
    auto* dock = new QFrame(area);
    dock->setObjectName(QStringLiteral("DockPanel"));
    auto* dockLayout = new QHBoxLayout(dock);
    dockLayout->setContentsMargins(8, 3, 8, 3);
    dockLayout->setSpacing(3);

    auto addDockButton = [dock, dockLayout](const QString& glyph, const QString& caption,
                                          const QColor& color) {
        auto* button = new QToolButton(dock);
        button->setObjectName(QStringLiteral("DockButton"));
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIcon(dockGlyph(glyph, color));
        button->setIconSize(QSize(25, 25));
        button->setText(caption);
        button->setCursor(Qt::PointingHandCursor);
        dockLayout->addWidget(button);
        return button;
    };

    auto* homeButton = addDockButton(QStringLiteral("⌂"), QStringLiteral("文件库"), QColor(QStringLiteral("#6487B8")));
    auto* searchDockButton = addDockButton(QStringLiteral("⌕"), QStringLiteral("搜索"), QColor(QStringLiteral("#7B8798")));
    auto* importDockButton = addDockButton(QStringLiteral("＋"), QStringLiteral("导入"), QColor(QStringLiteral("#5A9B7B")));
    auto* refreshDockButton = addDockButton(QStringLiteral("↻"), QStringLiteral("刷新"), QColor(QStringLiteral("#A17C55")));
    connect(homeButton, &QToolButton::clicked, this, [this]() {
        setCategoryFilter(QString(), QStringLiteral("全部文件"));
    });
    connect(searchDockButton, &QToolButton::clicked, this, [this]() {
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    });
    connect(importDockButton, &QToolButton::clicked, this, &MainWindow::chooseFiles);
    connect(refreshDockButton, &QToolButton::clicked, this, &MainWindow::runSearch);
    dockRow->addWidget(dock);
    dockRow->addStretch();
    layout->addLayout(dockRow);

    auto* statusRow = new QHBoxLayout();
    auto* footerCaption = new QLabel(QStringLiteral("WORKDESK  /  LOCAL FILE SPACE"), area);
    footerCaption->setObjectName(QStringLiteral("StatusLabel"));
    statusRow->addWidget(footerCaption);
    statusRow->addStretch();
    m_statusLabel = new QLabel(QStringLiteral("正在初始化本地索引…"), area);
    m_statusLabel->setObjectName(QStringLiteral("StatusLabel"));
    statusRow->addWidget(m_statusLabel);
    layout->addLayout(statusRow);

    connect(m_fileView, &QListView::doubleClicked, this, &MainWindow::onFileActivated);
    connect(m_fileView, &QListView::customContextMenuRequested,
            this, &MainWindow::onFileContextMenu);
    connect(m_fileView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex& current, const QModelIndex&) {
        updatePreview(current);
    });

    m_gridModeButton->setChecked(true);
    m_fileView->setCurrentIndex(QModelIndex());
    return area;
}
