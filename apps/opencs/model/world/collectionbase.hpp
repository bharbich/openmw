#ifndef CSM_WOLRD_COLLECTIONBASE_H
#define CSM_WOLRD_COLLECTIONBASE_H

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "columns.hpp"
#include "universalid.hpp"

class QVariant;

namespace ESM
{
    class RefId;
}

namespace CSMWorld
{
    struct ColumnBase;
    struct RecordBase;

    /// \brief Base class for record collections
    ///
    /// \attention Modifying records through the interface does not update connected views.
    /// Such modifications should be done through the table model interface instead unless no views
    /// are connected to the model or special precautions have been taken to send update signals
    /// manually.
    class CollectionBase
    {
        // not implemented
        CollectionBase(const CollectionBase&);
        CollectionBase& operator=(const CollectionBase&);

    public:
        CollectionBase();

        virtual ~CollectionBase();

        virtual int getSize() const = 0;

        virtual ESM::RefId getId(int index) const = 0;

        virtual int getIndex(const ESM::RefId& id) const = 0;

        virtual int getColumns() const = 0;

        virtual const ColumnBase& getColumn(int column) const = 0;

        virtual QVariant getData(int index, int column) const = 0;

        virtual void setData(int index, int column, const QVariant& data) = 0;

        // Not in use. Temporarily removed so that the implementation of RefIdCollection can continue without
        // these functions for now.
        //            virtual void merge() = 0;
        ///< Merge modified into base.

        //            virtual void purge() = 0;
        ///< Remove records that are flagged as erased.

        virtual void removeRows(int index, int count) = 0;

        virtual void appendBlankRecord(const ESM::RefId& id, UniversalId::Type type = UniversalId::Type_None) = 0;
        ///< \param type Will be ignored, unless the collection supports multiple record types

        virtual int searchId(const ESM::RefId& id) const = 0;
        ////< Search record with \a id.
        /// \return index of record (if found) or -1 (not found)

        virtual void replace(int index, std::unique_ptr<RecordBase> record) = 0;
        ///< If the record type does not match, an exception is thrown.
        ///
        /// \attention \a record must not change the ID.
        ///< \param type Will be ignored, unless the collection supports multiple record types

        virtual void appendRecord(std::unique_ptr<RecordBase> record, UniversalId::Type type = UniversalId::Type_None)
            = 0;
        ///< If the record type does not match, an exception is thrown.

        virtual void cloneRecord(const ESM::RefId& origin, const ESM::RefId& destination, const UniversalId::Type type)
            = 0;

        virtual bool touchRecord(const ESM::RefId& id) = 0;

        virtual const RecordBase& getRecord(const ESM::RefId& id) const = 0;

        virtual const RecordBase& getRecord(int index) const = 0;

        virtual int getAppendIndex(const ESM::RefId& id, UniversalId::Type type = UniversalId::Type_None) const = 0;
        ///< \param type Will be ignored, unless the collection supports multiple record types

        virtual std::vector<ESM::RefId> getIds(bool listDeleted = true) const = 0;
        ///< Return a sorted collection of all IDs
        ///
        /// \param listDeleted include deleted record in the list

        virtual bool reorderRows(int baseIndex, const std::vector<int>& newOrder) = 0;
        ///< Reorder the rows [baseIndex, baseIndex+newOrder.size()) according to the indices
        /// given in \a newOrder (baseIndex+newOrder[0] specifies the new index of row baseIndex).
        ///
        /// \return Success?

        virtual int getInsertIndex(
            const ESM::RefId& id, UniversalId::Type type = UniversalId::Type_None, RecordBase* record = nullptr) const;
        ///< Works like getAppendIndex unless an overloaded method uses the record pointer
        /// to get additional info about the record that results in an alternative index.

        int searchColumnIndex(Columns::ColumnId id) const;
        ///< Return index of column with the given \a id. If no such column exists, -1 is returned.

        int findColumnIndex(Columns::ColumnId id) const;
        ///< Return index of column with the given \a id. If no such column exists, an exception is
        /// thrown.
    };
}

#endif
