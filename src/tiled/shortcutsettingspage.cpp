/*
 * shortcutsettingspage.cpp
 * Copyright 2019, Thorbjørn Lindeijer <bjorn@lindeijer.nl>
 *
 * This file is part of Tiled.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "shortcutsettingspage.h"
#include "ui_shortcutsettingspage.h"

#include "actionmanager.h"
#include "preferences.h"
#include "savefile.h"
#include "utils.h"

#include <QAbstractListModel>
#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QItemEditorFactory>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QMessageBox>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <memory>

#include "qtcompat_p.h"

namespace Tiled {

/**
 * The ActionsModel makes the list of actions and their shortcuts available
 * to the view.
 */
class ActionsModel : public QAbstractListModel
{
public:
    enum UserRoles {
        HasCustomShortcut = Qt::UserRole
    };

    explicit ActionsModel(QObject *parent = nullptr);

    void setVisible(bool visible);
    void resetAllCustomShortcuts();
    void setCustomShortcuts(const QHash<Id, QKeySequence> &shortcuts);

    int rowCount(const QModelIndex &parent) const override;
    int columnCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    void refresh();
    void emitDataChanged(int row);
    void actionChanged(Id actionId);

    QList<Id> mActions;
    bool mDirty = false;
    bool mVisible = false;
};

ActionsModel::ActionsModel(QObject *parent)
    : QAbstractListModel(parent)
    , mActions(ActionManager::actions())
{
    connect(ActionManager::instance(), &ActionManager::actionChanged,
            this, &ActionsModel::actionChanged);
    connect(ActionManager::instance(), &ActionManager::actionsChanged,
            this, [this] { mDirty = true; refresh(); });
}

void ActionsModel::setVisible(bool visible)
{
    mVisible = visible;
    refresh();
}

void ActionsModel::refresh()
{
    if (mVisible && mDirty) {
        beginResetModel();
        mActions = ActionManager::actions();
        mDirty = false;
        endResetModel();
    }
}

void ActionsModel::resetAllCustomShortcuts()
{
    ActionManager::instance()->resetAllCustomShortcuts();

    emit dataChanged(index(0, 0),
                     index(mActions.size() - 1, 2),
                     QVector<int> { Qt::DisplayRole, Qt::EditRole, Qt::FontRole });
}

void ActionsModel::setCustomShortcuts(const QHash<Id, QKeySequence> &shortcuts)
{
    ActionManager::instance()->setCustomShortcuts(shortcuts);

    emit dataChanged(index(0, 0),
                     index(mActions.size() - 1, 2),
                     QVector<int> { Qt::DisplayRole, Qt::EditRole, Qt::FontRole });
}

int ActionsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : mActions.size();
}

int ActionsModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 3;
}

static QString strippedText(QString s)
{
    s.remove(QLatin1String("..."));
    for (int i = 0; i < s.size(); ++i) {
        if (s.at(i) == QLatin1Char('&'))
            s.remove(i, 1);
    }
    return s.trimmed();
}

QVariant ActionsModel::data(const QModelIndex &index, int role) const
{
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole: {
        const Id actionId = mActions.at(index.row());

        switch (index.column()) {
        case 0:
            return actionId.name();
        case 1:
            return strippedText(ActionManager::action(actionId)->text());
        case 2:
            return ActionManager::action(actionId)->shortcut();
        }

        break;
    }
    case Qt::FontRole: {
        const Id actionId = mActions.at(index.row());

        if (ActionManager::instance()->hasCustomShortcut(actionId)) {
            QFont font = QApplication::font();
            font.setBold(true);
            return font;
        }
        break;
    }
    case HasCustomShortcut: {
        const Id actionId = mActions.at(index.row());
        return ActionManager::instance()->hasCustomShortcut(actionId);
    }
    }

    return QVariant();
}

bool ActionsModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (index.column() == 2 && role == Qt::EditRole) {
        const Id actionId = mActions.at(index.row());
        const auto action = ActionManager::findAction(actionId);

        if (action) {
            auto actionManager = ActionManager::instance();

            // Null QVariant used for resetting shortcut to default
            if (value.isNull() && actionManager->hasCustomShortcut(actionId)) {
                actionManager->resetCustomShortcut(actionId);
                emitDataChanged(index.row());
                return true;
            }

            const auto keySequence = value.value<QKeySequence>();
            if (action->shortcut() != keySequence) {
                actionManager->setCustomShortcut(actionId, keySequence);
                // Guaranteed to trigger actionChanged, which emits dataChanged
                return true;
            }
        }
    }

    return false;
}

Qt::ItemFlags ActionsModel::flags(const QModelIndex &index) const
{
    auto f = QAbstractListModel::flags(index);

    if (index.column() == 2)
        f |= Qt::ItemIsEditable;

    return f;
}

QVariant ActionsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case 0:
            return tr("Action");
        case 1:
            return tr("Text");
        case 2:
            return tr("Shortcut");
        }
    }
    return QVariant();
}

void ActionsModel::emitDataChanged(int row)
{
    emit dataChanged(index(row, 0),
                     index(row, 2),
                     QVector<int> { Qt::DisplayRole, Qt::EditRole, Qt::FontRole });
}

void ActionsModel::actionChanged(Id actionId)
{
    int row = mActions.indexOf(actionId);
    if (row != -1)
        emitDataChanged(row);
}


/**
 * ShortcutEditor is needed to add a "Clear" button to the QKeySequenceEdit,
 * which doesn't feature one as of Qt 5.13. It also adds a button to reset
 * the shortcut.
 */
class ShortcutEditor : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QKeySequence keySequence READ keySequence WRITE setKeySequence NOTIFY keySequenceChanged USER true)

public:
    ShortcutEditor(QWidget *parent = nullptr);

    QKeySequence keySequence() const;

    void setResetEnabled(bool enabled);

public slots:
    void setKeySequence(QKeySequence keySequence);

signals:
    void resetRequested();
    void editingFinished();
    void keySequenceChanged(QKeySequence keySequence);

private:
    QKeySequenceEdit *mKeySequenceEdit;
    QToolButton *mResetButton;
};

ShortcutEditor::ShortcutEditor(QWidget *parent)
    : QWidget(parent)
    , mKeySequenceEdit(new QKeySequenceEdit)
{
    auto clearButton = new QToolButton(this);
    clearButton->setAutoRaise(true);
    clearButton->setAutoFillBackground(true);
    clearButton->setToolTip(tr("Remove shortcut"));
    clearButton->setEnabled(false);
    clearButton->setIcon(QIcon(QLatin1String("://images/scalable/edit-delete-symbolic.svg")));

    mResetButton = new QToolButton(this);
    mResetButton->setAutoRaise(true);
    mResetButton->setAutoFillBackground(true);
    mResetButton->setToolTip(tr("Reset shortcut to default"));
    mResetButton->setIcon(QIcon(QLatin1String("://images/scalable/edit-undo-symbolic.svg")));

    auto layout = new QHBoxLayout(this);
    layout->setMargin(0);
    layout->setSpacing(0);
    layout->addWidget(mKeySequenceEdit);
    layout->addWidget(clearButton);
    layout->addWidget(mResetButton);

    setFocusProxy(mKeySequenceEdit);

    connect(clearButton, &QToolButton::clicked,
            mKeySequenceEdit, &QKeySequenceEdit::clear);
    connect(mResetButton, &QToolButton::clicked,
            this, &ShortcutEditor::resetRequested);

    connect(mKeySequenceEdit, &QKeySequenceEdit::editingFinished,
            this, &ShortcutEditor::editingFinished);
    connect(mKeySequenceEdit, &QKeySequenceEdit::keySequenceChanged,
            this, &ShortcutEditor::keySequenceChanged);
    connect(mKeySequenceEdit, &QKeySequenceEdit::keySequenceChanged,
            this, [=] { clearButton->setEnabled(!keySequence().isEmpty()); });
}

QKeySequence ShortcutEditor::keySequence() const
{
    return mKeySequenceEdit->keySequence();
}

void ShortcutEditor::setResetEnabled(bool enabled)
{
    mResetButton->setEnabled(enabled);
}

void ShortcutEditor::setKeySequence(QKeySequence keySequence)
{
    mKeySequenceEdit->setKeySequence(keySequence);
}


/**
 * ShortcutDelegate subclass needed to make the editor close when it emits the
 * "editingFinished" signal.
 */
class ShortcutDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    ShortcutDelegate(QObject *parent = nullptr);

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;

private:
    std::unique_ptr<QItemEditorFactory> mItemEditorFactory;

};

ShortcutDelegate::ShortcutDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
    , mItemEditorFactory(new QItemEditorFactory)
{
    mItemEditorFactory->registerEditor(QVariant::KeySequence,
                                       new QStandardItemEditorCreator<ShortcutEditor>);

    setItemEditorFactory(mItemEditorFactory.get());
}

QWidget *ShortcutDelegate::createEditor(QWidget *parent,
                                        const QStyleOptionViewItem &option,
                                        const QModelIndex &index) const
{
    auto editor = QStyledItemDelegate::createEditor(parent, option, index);

    if (auto shortcutEditor = qobject_cast<ShortcutEditor*>(editor)) {
        shortcutEditor->setResetEnabled(index.data(ActionsModel::HasCustomShortcut).toBool());

        const QPersistentModelIndex persistentIndex(index);

        connect(shortcutEditor, &ShortcutEditor::keySequenceChanged, this, [=] {
            emit const_cast<ShortcutDelegate*>(this)->commitData(editor);
            shortcutEditor->setResetEnabled(index.data(ActionsModel::HasCustomShortcut).toBool());
        });

        connect(shortcutEditor, &ShortcutEditor::editingFinished, this, [=] {
            emit const_cast<ShortcutDelegate*>(this)->closeEditor(editor);
        });

        connect(shortcutEditor, &ShortcutEditor::resetRequested, this, [=] {
            auto model = const_cast<QAbstractItemModel*>(persistentIndex.model());
            model->setData(persistentIndex, QVariant(), Qt::EditRole);
            shortcutEditor->setKeySequence(index.data(Qt::EditRole).value<QKeySequence>());
            shortcutEditor->setResetEnabled(index.data(ActionsModel::HasCustomShortcut).toBool());
        });
    }

    return editor;
}


/**
 * Allows interactively resizing each column, but also stretches a single one
 * when the header gets resized.
 *
 * Based on HeaderViewStretcher from Qt Creator.
 */
class CustomStretchColumnHeaderView : public QHeaderView
{
    Q_OBJECT

public:
    CustomStretchColumnHeaderView(QWidget *parent = nullptr);

    void initialize();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    const int mColumnToStretch = 1;
};

CustomStretchColumnHeaderView::CustomStretchColumnHeaderView(QWidget *parent)
    : QHeaderView(Qt::Horizontal, parent)
{
    setStretchLastSection(true);
}

/**
 * Should be called after setting the model and the header on the view, but
 * before the view is shown.
 */
void CustomStretchColumnHeaderView::initialize()
{
    for (int i = 0; i < count(); ++i)
        setSectionResizeMode(i, i == mColumnToStretch ? Stretch : ResizeToContents);
}

void CustomStretchColumnHeaderView::resizeEvent(QResizeEvent *event)
{
    if (sectionResizeMode(mColumnToStretch) == QHeaderView::Interactive) {
        int diff = event->size().width() - event->oldSize().width();
        resizeSection(mColumnToStretch, qMax(32, sectionSize(mColumnToStretch) + diff));
    }
    QHeaderView::resizeEvent(event);
}

void CustomStretchColumnHeaderView::showEvent(QShowEvent *event)
{
    for (int i = 0; i < count(); ++i)
        setSectionResizeMode(i, QHeaderView::Interactive);
    QHeaderView::showEvent(event);
}

void CustomStretchColumnHeaderView::hideEvent(QHideEvent *event)
{
    initialize();
    QHeaderView::hideEvent(event);
}


/**
 * The actual settings page for editing keyboard shortcuts.
 */
ShortcutSettingsPage::ShortcutSettingsPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ShortcutSettingsPage)
    , mActionsModel(new ActionsModel(this))
    , mProxyModel(new QSortFilterProxyModel(this))
{
    ui->setupUi(this);

    mProxyModel->setSourceModel(mActionsModel);
    mProxyModel->setSortLocaleAware(true);
    mProxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    mProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

    auto header = new CustomStretchColumnHeaderView(this);

    ui->shortcutsView->setModel(mProxyModel);
    ui->shortcutsView->setHeader(header);
    ui->shortcutsView->sortByColumn(0, Qt::AscendingOrder);
    ui->shortcutsView->setItemDelegateForColumn(2, new ShortcutDelegate);

    header->initialize();

    connect(ui->filterEdit, &QLineEdit::textChanged,
            mProxyModel, &QSortFilterProxyModel::setFilterFixedString);

    connect(ui->resetButton, &QPushButton::clicked,
            mActionsModel, &ActionsModel::resetAllCustomShortcuts);

    connect(ui->importButton, &QPushButton::clicked,
            this, &ShortcutSettingsPage::importShortcuts);
    connect(ui->exportButton, &QPushButton::clicked,
            this, &ShortcutSettingsPage::exportShortcuts);
}

ShortcutSettingsPage::~ShortcutSettingsPage()
{
    QWidget *w = ui->shortcutsView->indexWidget(ui->shortcutsView->currentIndex());
    if (auto shortcutEditor = qobject_cast<ShortcutEditor*>(w))
        emit shortcutEditor->editingFinished();

    delete ui;
}

QSize ShortcutSettingsPage::sizeHint() const
{
    QSize size = QWidget::sizeHint();
    size.setWidth(qRound(Utils::dpiScaled(500)));
    return size;
}

void ShortcutSettingsPage::showEvent(QShowEvent *event)
{
    mActionsModel->setVisible(true);
    QWidget::showEvent(event);
}

void ShortcutSettingsPage::hideEvent(QHideEvent *event)
{
    mActionsModel->setVisible(false);
    QWidget::hideEvent(event);
}

void ShortcutSettingsPage::importShortcuts()
{
    QString filter = tr("Keyboard Mapping Scheme (*.kms)");
    QString fileName = QFileDialog::getOpenFileName(this, tr("Import Shortcuts"),
                                                    QString(), filter);

    if (fileName.isEmpty())
        return;

    QFile file(fileName);
    if (!file.open(QFile::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this,
                              tr("Error Loading Shortcuts"),
                              tr("Could not open file for reading."));
        return;
    }

    QXmlStreamReader xml(&file);

    if (xml.readNextStartElement() && xml.name() == QLatin1String("mapping")) {
        QHash<Id, QKeySequence> result;

        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("shortcut")) {
                QStringRef id = xml.attributes().value(QLatin1String("id"));

                while (xml.readNextStartElement()) {
                    if (xml.name() == QLatin1String("key")) {
                        QString keyString = xml.attributes().value(QLatin1String("value")).toString();
                        result.insert(Id(id.toUtf8()), QKeySequence(keyString));
                        xml.skipCurrentElement();   // skip out of "key" element
                        xml.skipCurrentElement();   // skip out of "shortcut" element
                        break;
                    } else {
                        xml.skipCurrentElement();   // skip unknown element
                    }
                }
            } else {
                xml.skipCurrentElement();           // skip unknown element
            }
        }

        mActionsModel->setCustomShortcuts(result);
    }
}

void ShortcutSettingsPage::exportShortcuts()
{
    QString filter = tr("Keyboard Mapping Scheme (*.kms)");
    QString fileName = QFileDialog::getSaveFileName(this, tr("Export Shortcuts"),
                                                    QString(), filter);

    if (fileName.isEmpty())
        return;

    SaveFile file(fileName);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this,
                              tr("Error Saving Shortcuts"),
                              tr("Could not open file for writing."));
        return;
    }

    QXmlStreamWriter xml(file.device());

    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(1);

    xml.writeStartDocument();
    xml.writeDTD(QLatin1String("<!DOCTYPE KeyboardMappingScheme>"));
    xml.writeComment(QString::fromLatin1(" Written by %1 %2, %3. ").
                     arg(QApplication::applicationDisplayName(),
                         QApplication::applicationVersion(),
                         QDateTime::currentDateTime().toString(Qt::ISODate)));
    xml.writeStartElement(QLatin1String("mapping"));

    auto actions = ActionManager::actions();
    std::sort(actions.begin(), actions.end());

    for (Id actionId : qAsConst(actions)) {
        const auto action = ActionManager::action(actionId);
        const auto shortcut = action->shortcut();

        xml.writeStartElement(QLatin1String("shortcut"));
        xml.writeAttribute(QLatin1String("id"), actionId.toString());

        if (!shortcut.isEmpty()) {
            xml.writeEmptyElement(QLatin1String("key"));
            xml.writeAttribute(QLatin1String("value"), shortcut.toString());
        }

        xml.writeEndElement();  // shortcut
    }

    xml.writeEndElement();  // mapping
    xml.writeEndDocument();

    if (!file.commit()) {
        QMessageBox::critical(this,
                              tr("Error Saving Shortcuts"),
                              file.errorString());
    }
}

} // namespace Tiled

#include "shortcutsettingspage.moc"
