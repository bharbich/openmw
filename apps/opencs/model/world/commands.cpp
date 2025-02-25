#include "commands.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_set>

#include <apps/opencs/model/world/columns.hpp>
#include <apps/opencs/model/world/land.hpp>
#include <apps/opencs/model/world/landtexture.hpp>
#include <apps/opencs/model/world/record.hpp>
#include <apps/opencs/model/world/universalid.hpp>

#include <components/esm3/loadland.hpp>
#include <components/esm3/loadpgrd.hpp>
#include <components/misc/constants.hpp>

#include <QAbstractItemModel>
#include <QAbstractProxyModel>

#include "cellcoordinates.hpp"
#include "idtable.hpp"
#include "idtree.hpp"
#include "nestedtablewrapper.hpp"
#include "pathgrid.hpp"

CSMWorld::TouchCommand::TouchCommand(IdTable& table, const std::string& id, QUndoCommand* parent)
    : QUndoCommand(parent)
    , mTable(table)
    , mId(id)
    , mOld(nullptr)
    , mChanged(false)
{
    setText(("Touch " + mId).c_str());
}

void CSMWorld::TouchCommand::redo()
{
    mOld.reset(mTable.getRecord(mId).clone().get());
    mChanged = mTable.touchRecord(mId);
}

void CSMWorld::TouchCommand::undo()
{
    if (mChanged)
    {
        mTable.setRecord(mId, std::move(mOld));
        mChanged = false;
    }
}

CSMWorld::ImportLandTexturesCommand::ImportLandTexturesCommand(
    IdTable& landTable, IdTable& ltexTable, QUndoCommand* parent)
    : QUndoCommand(parent)
    , mLands(landTable)
    , mLtexs(ltexTable)
    , mOldState(0)
{
    setText("Copy land textures to current plugin");
}

void CSMWorld::ImportLandTexturesCommand::redo()
{
    int pluginColumn = mLands.findColumnIndex(Columns::ColumnId_PluginIndex);
    int oldPlugin = mLands.data(mLands.getModelIndex(getOriginId(), pluginColumn)).toInt();

    // Original data
    int textureColumn = mLands.findColumnIndex(Columns::ColumnId_LandTexturesIndex);
    mOld = mLands.data(mLands.getModelIndex(getOriginId(), textureColumn)).value<DataType>();

    // Need to make a copy so the old values can be looked up
    DataType copy(mOld);

    // Perform touch/copy/etc...
    onRedo();

    // Find all indices used
    std::unordered_set<int> texIndices;
    for (int i = 0; i < mOld.size(); ++i)
    {
        // All indices are offset by 1 for a default texture
        if (mOld[i] > 0)
            texIndices.insert(mOld[i] - 1);
    }

    std::vector<std::string> oldTextures;
    oldTextures.reserve(texIndices.size());
    for (int index : texIndices)
    {
        oldTextures.push_back(LandTexture::createUniqueRecordId(oldPlugin, index));
    }

    // Import the textures, replace old values
    LandTextureIdTable::ImportResults results = dynamic_cast<LandTextureIdTable&>(mLtexs).importTextures(oldTextures);
    mCreatedTextures = std::move(results.createdRecords);
    for (const auto& it : results.recordMapping)
    {
        int plugin = 0, newIndex = 0, oldIndex = 0;
        LandTexture::parseUniqueRecordId(it.first, plugin, oldIndex);
        LandTexture::parseUniqueRecordId(it.second, plugin, newIndex);

        if (newIndex != oldIndex)
        {
            for (int i = 0; i < Land::LAND_NUM_TEXTURES; ++i)
            {
                // All indices are offset by 1 for a default texture
                if (mOld[i] == oldIndex + 1)
                    copy[i] = newIndex + 1;
            }
        }
    }

    // Apply modification
    int stateColumn = mLands.findColumnIndex(Columns::ColumnId_Modification);
    mOldState = mLands.data(mLands.getModelIndex(getDestinationId(), stateColumn)).toInt();

    QVariant variant;
    variant.setValue(copy);
    mLands.setData(mLands.getModelIndex(getDestinationId(), textureColumn), variant);
}

void CSMWorld::ImportLandTexturesCommand::undo()
{
    // Restore to previous
    int textureColumn = mLands.findColumnIndex(Columns::ColumnId_LandTexturesIndex);
    QVariant variant;
    variant.setValue(mOld);
    mLands.setData(mLands.getModelIndex(getDestinationId(), textureColumn), variant);

    int stateColumn = mLands.findColumnIndex(Columns::ColumnId_Modification);
    mLands.setData(mLands.getModelIndex(getDestinationId(), stateColumn), mOldState);

    // Undo copy/touch/etc...
    onUndo();

    for (const std::string& id : mCreatedTextures)
    {
        int row = mLtexs.getModelIndex(id, 0).row();
        mLtexs.removeRows(row, 1);
    }
    mCreatedTextures.clear();
}

CSMWorld::CopyLandTexturesCommand::CopyLandTexturesCommand(
    IdTable& landTable, IdTable& ltexTable, const std::string& origin, const std::string& dest, QUndoCommand* parent)
    : ImportLandTexturesCommand(landTable, ltexTable, parent)
    , mOriginId(origin)
    , mDestId(dest)
{
}

const std::string& CSMWorld::CopyLandTexturesCommand::getOriginId() const
{
    return mOriginId;
}

const std::string& CSMWorld::CopyLandTexturesCommand::getDestinationId() const
{
    return mDestId;
}

CSMWorld::TouchLandCommand::TouchLandCommand(
    IdTable& landTable, IdTable& ltexTable, const std::string& id, QUndoCommand* parent)
    : ImportLandTexturesCommand(landTable, ltexTable, parent)
    , mId(id)
    , mOld(nullptr)
    , mChanged(false)
{
    setText(("Touch " + mId).c_str());
}

const std::string& CSMWorld::TouchLandCommand::getOriginId() const
{
    return mId;
}

const std::string& CSMWorld::TouchLandCommand::getDestinationId() const
{
    return mId;
}

void CSMWorld::TouchLandCommand::onRedo()
{
    mChanged = mLands.touchRecord(mId);
    if (mChanged)
        mOld.reset(mLands.getRecord(mId).clone().get());
}

void CSMWorld::TouchLandCommand::onUndo()
{
    if (mChanged)
    {
        mLands.setRecord(mId, std::move(mOld));
        mChanged = false;
    }
}

CSMWorld::ModifyCommand::ModifyCommand(
    QAbstractItemModel& model, const QModelIndex& index, const QVariant& new_, QUndoCommand* parent)
    : QUndoCommand(parent)
    , mModel(&model)
    , mIndex(index)
    , mNew(new_)
    , mHasRecordState(false)
    , mOldRecordState(CSMWorld::RecordBase::State_BaseOnly)
{
    if (QAbstractProxyModel* proxy = dynamic_cast<QAbstractProxyModel*>(mModel))
    {
        // Replace proxy with actual model
        mIndex = proxy->mapToSource(mIndex);
        mModel = proxy->sourceModel();
    }
}

void CSMWorld::ModifyCommand::redo()
{
    if (mIndex.parent().isValid())
    {
        CSMWorld::IdTree* tree = &dynamic_cast<CSMWorld::IdTree&>(*mModel);
        setText("Modify "
            + tree->nestedHeaderData(mIndex.parent().column(), mIndex.column(), Qt::Horizontal, Qt::DisplayRole)
                  .toString());
    }
    else
    {
        setText("Modify " + mModel->headerData(mIndex.column(), Qt::Horizontal, Qt::DisplayRole).toString());
    }

    // Remember record state before the modification
    if (CSMWorld::IdTable* table = dynamic_cast<IdTable*>(mModel))
    {
        mHasRecordState = true;
        int stateColumnIndex = table->findColumnIndex(Columns::ColumnId_Modification);

        int rowIndex = mIndex.row();
        if (mIndex.parent().isValid())
        {
            rowIndex = mIndex.parent().row();
        }

        mRecordStateIndex = table->index(rowIndex, stateColumnIndex);
        mOldRecordState = static_cast<CSMWorld::RecordBase::State>(table->data(mRecordStateIndex).toInt());
    }

    mOld = mModel->data(mIndex, Qt::EditRole);
    mModel->setData(mIndex, mNew);
}

void CSMWorld::ModifyCommand::undo()
{
    mModel->setData(mIndex, mOld);
    if (mHasRecordState)
    {
        mModel->setData(mRecordStateIndex, mOldRecordState);
    }
}

void CSMWorld::CreateCommand::applyModifications()
{
    if (!mNestedValues.empty())
    {
        CSMWorld::IdTree* tree = &dynamic_cast<CSMWorld::IdTree&>(mModel);
        std::map<int, std::pair<int, QVariant>>::const_iterator current = mNestedValues.begin();
        std::map<int, std::pair<int, QVariant>>::const_iterator end = mNestedValues.end();
        for (; current != end; ++current)
        {
            QModelIndex index = tree->index(0, current->second.first, tree->getNestedModelIndex(mId, current->first));
            tree->setData(index, current->second.second);
        }
    }
}

CSMWorld::CreateCommand::CreateCommand(IdTable& model, const std::string& id, QUndoCommand* parent)
    : QUndoCommand(parent)
    , mModel(model)
    , mId(id)
    , mType(UniversalId::Type_None)
{
    setText(("Create record " + id).c_str());
}

void CSMWorld::CreateCommand::addValue(int column, const QVariant& value)
{
    mValues[column] = value;
}

void CSMWorld::CreateCommand::addNestedValue(int parentColumn, int nestedColumn, const QVariant& value)
{
    mNestedValues[parentColumn] = std::make_pair(nestedColumn, value);
}

void CSMWorld::CreateCommand::setType(UniversalId::Type type)
{
    mType = type;
}

void CSMWorld::CreateCommand::redo()
{
    mModel.addRecordWithData(mId, mValues, mType);
    applyModifications();
}

void CSMWorld::CreateCommand::undo()
{
    mModel.removeRow(mModel.getModelIndex(mId, 0).row());
}

CSMWorld::RevertCommand::RevertCommand(IdTable& model, const std::string& id, QUndoCommand* parent)
    : QUndoCommand(parent)
    , mModel(model)
    , mId(id)
    , mOld(nullptr)
{
    setText(("Revert record " + id).c_str());
}

CSMWorld::RevertCommand::~RevertCommand() {}

void CSMWorld::RevertCommand::redo()
{
    mOld = mModel.getRecord(mId).clone();

    int column = mModel.findColumnIndex(Columns::ColumnId_Modification);

    QModelIndex index = mModel.getModelIndex(mId, column);
    RecordBase::State state = static_cast<RecordBase::State>(mModel.data(index).toInt());

    if (state == RecordBase::State_ModifiedOnly)
    {
        mModel.removeRows(index.row(), 1);
    }
    else
    {
        mModel.setData(index, static_cast<int>(RecordBase::State_BaseOnly));
    }
}

void CSMWorld::RevertCommand::undo()
{
    mModel.setRecord(mId, std::move(mOld));
}

CSMWorld::DeleteCommand::DeleteCommand(
    IdTable& model, const std::string& id, CSMWorld::UniversalId::Type type, QUndoCommand* parent)
    : QUndoCommand(parent)
    , mModel(model)
    , mId(id)
    , mOld(nullptr)
    , mType(type)
{
    setText(("Delete record " + id).c_str());
}

CSMWorld::DeleteCommand::~DeleteCommand() {}

void CSMWorld::DeleteCommand::redo()
{
    mOld = mModel.getRecord(mId).clone();

    int column = mModel.findColumnIndex(Columns::ColumnId_Modification);

    QModelIndex index = mModel.getModelIndex(mId, column);
    RecordBase::State state = static_cast<RecordBase::State>(mModel.data(index).toInt());

    if (state == RecordBase::State_ModifiedOnly)
    {
        mModel.removeRows(index.row(), 1);
    }
    else
    {
        mModel.setData(index, static_cast<int>(RecordBase::State_Deleted));
    }
}

void CSMWorld::DeleteCommand::undo()
{
    mModel.setRecord(mId, std::move(mOld), mType);
}

CSMWorld::ReorderRowsCommand::ReorderRowsCommand(IdTable& model, int baseIndex, const std::vector<int>& newOrder)
    : mModel(model)
    , mBaseIndex(baseIndex)
    , mNewOrder(newOrder)
{
}

void CSMWorld::ReorderRowsCommand::redo()
{
    mModel.reorderRows(mBaseIndex, mNewOrder);
}

void CSMWorld::ReorderRowsCommand::undo()
{
    int size = static_cast<int>(mNewOrder.size());
    std::vector<int> reverse(size);

    for (int i = 0; i < size; ++i)
        reverse.at(mNewOrder[i]) = i;

    mModel.reorderRows(mBaseIndex, reverse);
}

CSMWorld::CloneCommand::CloneCommand(CSMWorld::IdTable& model, const std::string& idOrigin,
    const std::string& idDestination, const CSMWorld::UniversalId::Type type, QUndoCommand* parent)
    : CreateCommand(model, idDestination, parent)
    , mIdOrigin(idOrigin)
{
    setType(type);
    setText(("Clone record " + idOrigin + " to the " + idDestination).c_str());
}

void CSMWorld::CloneCommand::redo()
{
    mModel.cloneRecord(ESM::RefId::stringRefId(mIdOrigin), ESM::RefId::stringRefId(mId), mType);
    applyModifications();
    for (auto& value : mOverrideValues)
    {
        mModel.setData(mModel.getModelIndex(mId, value.first), value.second);
    }
}

void CSMWorld::CloneCommand::undo()
{
    mModel.removeRow(mModel.getModelIndex(mId, 0).row());
}

void CSMWorld::CloneCommand::setOverrideValue(int column, QVariant value)
{
    mOverrideValues.emplace_back(std::make_pair(column, value));
}

CSMWorld::CreatePathgridCommand::CreatePathgridCommand(IdTable& model, const std::string& id, QUndoCommand* parent)
    : CreateCommand(model, id, parent)
{
    setType(UniversalId::Type_Pathgrid);
}

void CSMWorld::CreatePathgridCommand::redo()
{
    CreateCommand::redo();

    std::unique_ptr<Record<Pathgrid>> record
        = std::make_unique<Record<Pathgrid>>(static_cast<const Record<Pathgrid>&>(mModel.getRecord(mId)));
    record->get().blank();
    record->get().mCell = ESM::RefId::stringRefId(mId);

    std::pair<CellCoordinates, bool> coords = CellCoordinates::fromId(mId);
    if (coords.second)
    {
        record->get().mData.mX = coords.first.getX();
        record->get().mData.mY = coords.first.getY();
    }

    mModel.setRecord(mId, std::move(record), mType);
}

CSMWorld::UpdateCellCommand::UpdateCellCommand(IdTable& model, int row, QUndoCommand* parent)
    : QUndoCommand(parent)
    , mModel(model)
    , mRow(row)
{
    setText("Update cell ID");
}

void CSMWorld::UpdateCellCommand::redo()
{
    if (!mNew.isValid())
    {
        int cellColumn = mModel.searchColumnIndex(Columns::ColumnId_Cell);
        mIndex = mModel.index(mRow, cellColumn);

        QModelIndex xIndex = mModel.index(mRow, mModel.findColumnIndex(Columns::ColumnId_PositionXPos));

        QModelIndex yIndex = mModel.index(mRow, mModel.findColumnIndex(Columns::ColumnId_PositionYPos));

        int x = std::floor(mModel.data(xIndex).toFloat() / Constants::CellSizeInUnits);
        int y = std::floor(mModel.data(yIndex).toFloat() / Constants::CellSizeInUnits);

        std::ostringstream stream;

        stream << "#" << x << " " << y;

        mNew = QString::fromUtf8(stream.str().c_str());
    }

    mModel.setData(mIndex, mNew);
}

void CSMWorld::UpdateCellCommand::undo()
{
    mModel.setData(mIndex, mOld);
}

CSMWorld::DeleteNestedCommand::DeleteNestedCommand(
    IdTree& model, const std::string& id, int nestedRow, int parentColumn, QUndoCommand* parent)
    : QUndoCommand(parent)
    , NestedTableStoring(model, id, parentColumn)
    , mModel(model)
    , mId(id)
    , mParentColumn(parentColumn)
    , mNestedRow(nestedRow)
{
    std::string title = model.headerData(parentColumn, Qt::Horizontal, Qt::DisplayRole).toString().toUtf8().constData();
    setText(("Delete row in " + title + " sub-table of " + mId).c_str());

    QModelIndex parentIndex = mModel.getModelIndex(mId, mParentColumn);
    mModifyParentCommand = new ModifyCommand(mModel, parentIndex, parentIndex.data(Qt::EditRole), this);
}

void CSMWorld::DeleteNestedCommand::redo()
{
    QModelIndex parentIndex = mModel.getModelIndex(mId, mParentColumn);
    mModifyParentCommand->redo();
    mModel.removeRows(mNestedRow, 1, parentIndex);
}

void CSMWorld::DeleteNestedCommand::undo()
{
    QModelIndex parentIndex = mModel.getModelIndex(mId, mParentColumn);
    mModel.setNestedTable(parentIndex, getOld());
    mModifyParentCommand->undo();
}

CSMWorld::AddNestedCommand::AddNestedCommand(
    IdTree& model, const std::string& id, int nestedRow, int parentColumn, QUndoCommand* parent)
    : QUndoCommand(parent)
    , NestedTableStoring(model, id, parentColumn)
    , mModel(model)
    , mId(id)
    , mNewRow(nestedRow)
    , mParentColumn(parentColumn)
{
    std::string title = model.headerData(parentColumn, Qt::Horizontal, Qt::DisplayRole).toString().toUtf8().constData();
    setText(("Add row in " + title + " sub-table of " + mId).c_str());

    QModelIndex parentIndex = mModel.getModelIndex(mId, mParentColumn);
    mModifyParentCommand = new ModifyCommand(mModel, parentIndex, parentIndex.data(Qt::EditRole), this);
}

void CSMWorld::AddNestedCommand::redo()
{
    QModelIndex parentIndex = mModel.getModelIndex(mId, mParentColumn);
    mModifyParentCommand->redo();
    mModel.addNestedRow(parentIndex, mNewRow);
}

void CSMWorld::AddNestedCommand::undo()
{
    QModelIndex parentIndex = mModel.getModelIndex(mId, mParentColumn);
    mModel.setNestedTable(parentIndex, getOld());
    mModifyParentCommand->undo();
}

CSMWorld::NestedTableStoring::NestedTableStoring(const IdTree& model, const std::string& id, int parentColumn)
    : mOld(model.nestedTable(model.getModelIndex(id, parentColumn)))
{
}

CSMWorld::NestedTableStoring::~NestedTableStoring()
{
    delete mOld;
}

const CSMWorld::NestedTableWrapperBase& CSMWorld::NestedTableStoring::getOld() const
{
    return *mOld;
}
