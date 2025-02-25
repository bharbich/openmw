#ifndef CSV_WORLD_ENUMDELEGATE_H
#define CSV_WORLD_ENUMDELEGATE_H

#include <string>
#include <utility>
#include <vector>

#include <QString>

#include <apps/opencs/model/world/columnbase.hpp>

#include "util.hpp"

namespace CSMDoc
{
    class Document;
}

namespace CSMWorld
{
    class CommandDispatcher;
}

namespace CSVWorld
{
    /// \brief Integer value that represents an enum and is interacted with via a combobox
    class EnumDelegate : public CommandDelegate
    {
    protected:
        std::vector<std::pair<int, QString>> mValues;

        int getValueIndex(const QModelIndex& index, int role = Qt::DisplayRole) const;

    private:
        void setModelDataImp(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override;

        virtual void addCommands(QAbstractItemModel* model, const QModelIndex& index, int type) const;

    public:
        EnumDelegate(const std::vector<std::pair<int, QString>>& values, CSMWorld::CommandDispatcher* dispatcher,
            CSMDoc::Document& document, QObject* parent);

        QWidget* createEditor(
            QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

        QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index,
            CSMWorld::ColumnBase::Display display = CSMWorld::ColumnBase::Display_None) const override;

        void setEditorData(QWidget* editor, const QModelIndex& index, bool tryDisplay = false) const override;

        void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

        QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    };

    class EnumDelegateFactory : public CommandDelegateFactory
    {
    protected:
        std::vector<std::pair<int, QString>> mValues;

    public:
        EnumDelegateFactory();

        EnumDelegateFactory(const char** names, bool allowNone = false);
        ///< \param names Array of char pointer with a 0-pointer as end mark
        /// \param allowNone Use value of -1 for "none selected" (empty string)

        EnumDelegateFactory(const std::vector<std::pair<int, std::string>>& names, bool allowNone = false);
        /// \param allowNone Use value of -1 for "none selected" (empty string)

        CommandDelegate* makeDelegate(
            CSMWorld::CommandDispatcher* dispatcher, CSMDoc::Document& document, QObject* parent) const override;
        ///< The ownership of the returned CommandDelegate is transferred to the caller.

        void add(int value, const QString& name);
    };

}

#endif
