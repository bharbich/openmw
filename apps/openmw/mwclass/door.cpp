#include "door.hpp"

#include <MyGUI_TextIterator.h>

#include <components/esm3/doorstate.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwphysics/physicssystem.hpp"
#include "../mwworld/actiondoor.hpp"
#include "../mwworld/actionteleport.hpp"
#include "../mwworld/actiontrap.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/cellutils.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/customdata.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/failedaction.hpp"
#include "../mwworld/ptr.hpp"

#include "../mwgui/tooltips.hpp"
#include "../mwgui/ustring.hpp"

#include "../mwrender/animation.hpp"
#include "../mwrender/objects.hpp"
#include "../mwrender/renderinginterface.hpp"
#include "../mwrender/vismask.hpp"

#include "../mwmechanics/actorutil.hpp"

#include "classmodel.hpp"

namespace MWClass
{
    class DoorCustomData : public MWWorld::TypedCustomData<DoorCustomData>
    {
    public:
        MWWorld::DoorState mDoorState = MWWorld::DoorState::Idle;

        DoorCustomData& asDoorCustomData() override { return *this; }
        const DoorCustomData& asDoorCustomData() const override { return *this; }
    };

    Door::Door()
        : MWWorld::RegisteredClass<Door>(ESM::Door::sRecordId)
    {
    }

    void Door::insertObjectRendering(
        const MWWorld::Ptr& ptr, const std::string& model, MWRender::RenderingInterface& renderingInterface) const
    {
        if (!model.empty())
        {
            renderingInterface.getObjects().insertModel(ptr, model);
            ptr.getRefData().getBaseNode()->setNodeMask(MWRender::Mask_Static);
        }
    }

    void Door::insertObject(const MWWorld::Ptr& ptr, const std::string& model, const osg::Quat& rotation,
        MWPhysics::PhysicsSystem& physics) const
    {
        insertObjectPhysics(ptr, model, rotation, physics);

        // Resume the door's opening/closing animation if it wasn't finished
        if (ptr.getRefData().getCustomData())
        {
            const DoorCustomData& customData = ptr.getRefData().getCustomData()->asDoorCustomData();
            if (customData.mDoorState != MWWorld::DoorState::Idle)
            {
                MWBase::Environment::get().getWorld()->activateDoor(ptr, customData.mDoorState);
            }
        }
    }

    void Door::insertObjectPhysics(const MWWorld::Ptr& ptr, const std::string& model, const osg::Quat& rotation,
        MWPhysics::PhysicsSystem& physics) const
    {
        physics.addObject(ptr, model, rotation, MWPhysics::CollisionType_Door);
    }

    bool Door::isDoor() const
    {
        return true;
    }

    bool Door::useAnim() const
    {
        return true;
    }

    std::string Door::getModel(const MWWorld::ConstPtr& ptr) const
    {
        return getClassModel<ESM::Door>(ptr);
    }

    std::string_view Door::getName(const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::LiveCellRef<ESM::Door>* ref = ptr.get<ESM::Door>();
        const std::string& name = ref->mBase->mName;

        return !name.empty() ? name : ref->mBase->mId.getRefIdString();
    }

    std::unique_ptr<MWWorld::Action> Door::activate(const MWWorld::Ptr& ptr, const MWWorld::Ptr& actor) const
    {
        MWWorld::LiveCellRef<ESM::Door>* ref = ptr.get<ESM::Door>();

        const ESM::RefId& openSound = ref->mBase->mOpenSound;
        const ESM::RefId& closeSound = ref->mBase->mCloseSound;
        const ESM::RefId lockedSound = ESM::RefId::stringRefId("LockedDoor");

        // FIXME: If NPC activate teleporting door, it can lead to crash due to iterator invalidation in the Actors
        // update. Make such activation a no-op for now, like how it is in the vanilla game.
        if (actor != MWMechanics::getPlayer() && ptr.getCellRef().getTeleport())
        {
            std::unique_ptr<MWWorld::Action> action = std::make_unique<MWWorld::FailedAction>(std::string_view{}, ptr);
            action->setSound(lockedSound);
            return action;
        }

        // make door glow if player activates it with telekinesis
        if (actor == MWMechanics::getPlayer()
            && MWBase::Environment::get().getWorld()->getDistanceToFacedObject()
                > MWBase::Environment::get().getWorld()->getMaxActivationDistance())
        {
            MWRender::Animation* animation = MWBase::Environment::get().getWorld()->getAnimation(ptr);
            if (animation)
            {
                const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
                int index = ESM::MagicEffect::effectStringToId("sEffectTelekinesis");
                const ESM::MagicEffect* effect = store.get<ESM::MagicEffect>().find(index);

                animation->addSpellCastGlow(
                    effect, 1); // 1 second glow to match the time taken for a door opening or closing
            }
        }

        MWWorld::ContainerStore& invStore = actor.getClass().getContainerStore(actor);

        bool isLocked = ptr.getCellRef().getLockLevel() > 0;
        bool isTrapped = !ptr.getCellRef().getTrap().empty();
        bool hasKey = false;
        std::string_view keyName;
        const ESM::RefId& keyId = ptr.getCellRef().getKey();
        if (!keyId.empty())
        {
            MWWorld::Ptr keyPtr = invStore.search(keyId);
            if (!keyPtr.isEmpty())
            {
                hasKey = true;
                keyName = keyPtr.getClass().getName(keyPtr);
            }
        }

        if (isLocked && hasKey)
        {
            if (actor == MWMechanics::getPlayer())
                MWBase::Environment::get().getWindowManager()->messageBox(std::string{ keyName } + " #{sKeyUsed}");
            ptr.getCellRef().unlock(); // Call the function here. because that makes sense.
            // using a key disarms the trap
            if (isTrapped)
            {
                ptr.getCellRef().setTrap(ESM::RefId());
                MWBase::Environment::get().getSoundManager()->playSound3D(
                    ptr, ESM::RefId::stringRefId("Disarm Trap"), 1.0f, 1.0f);
                isTrapped = false;
            }
        }

        if (!isLocked || hasKey)
        {
            if (isTrapped)
            {
                // Trap activation
                std::unique_ptr<MWWorld::Action> action
                    = std::make_unique<MWWorld::ActionTrap>(ptr.getCellRef().getTrap(), ptr);
                action->setSound(ESM::RefId::stringRefId("Disarm Trap Fail"));
                return action;
            }

            if (ptr.getCellRef().getTeleport())
            {
                if (actor == MWMechanics::getPlayer()
                    && MWBase::Environment::get().getWorld()->getDistanceToFacedObject()
                        > MWBase::Environment::get().getWorld()->getMaxActivationDistance())
                {
                    // player activated teleport door with telekinesis
                    return std::make_unique<MWWorld::FailedAction>();
                }
                else
                {
                    std::unique_ptr<MWWorld::Action> action = std::make_unique<MWWorld::ActionTeleport>(
                        ptr.getCellRef().getDestCell(), ptr.getCellRef().getDoorDest(), true);
                    action->setSound(openSound);
                    return action;
                }
            }
            else
            {
                // animated door
                std::unique_ptr<MWWorld::Action> action = std::make_unique<MWWorld::ActionDoor>(ptr);
                const auto doorState = getDoorState(ptr);
                bool opening = true;
                float doorRot = ptr.getRefData().getPosition().rot[2] - ptr.getCellRef().getPosition().rot[2];
                if (doorState == MWWorld::DoorState::Opening)
                    opening = false;
                if (doorState == MWWorld::DoorState::Idle && doorRot != 0)
                    opening = false;

                if (opening)
                {
                    MWBase::Environment::get().getSoundManager()->fadeOutSound3D(ptr, closeSound, 0.5f);
                    // Doors rotate at 90 degrees per second, so start the sound at
                    // where it would be at the current rotation.
                    float offset = doorRot / (osg::PI * 0.5f);
                    action->setSoundOffset(offset);
                    action->setSound(openSound);
                }
                else
                {
                    MWBase::Environment::get().getSoundManager()->fadeOutSound3D(ptr, openSound, 0.5f);
                    float offset = 1.0f - doorRot / (osg::PI * 0.5f);
                    action->setSoundOffset(std::max(offset, 0.0f));
                    action->setSound(closeSound);
                }

                return action;
            }
        }
        else
        {
            // locked, and we can't open.
            std::unique_ptr<MWWorld::Action> action = std::make_unique<MWWorld::FailedAction>(std::string_view{}, ptr);
            action->setSound(lockedSound);
            return action;
        }
    }

    bool Door::canLock(const MWWorld::ConstPtr& ptr) const
    {
        return true;
    }

    bool Door::allowTelekinesis(const MWWorld::ConstPtr& ptr) const
    {
        if (ptr.getCellRef().getTeleport() && ptr.getCellRef().getLockLevel() <= 0
            && ptr.getCellRef().getTrap().empty())
            return false;
        else
            return true;
    }

    const ESM::RefId& Door::getScript(const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::LiveCellRef<ESM::Door>* ref = ptr.get<ESM::Door>();

        return ref->mBase->mScript;
    }

    MWGui::ToolTipInfo Door::getToolTipInfo(const MWWorld::ConstPtr& ptr, int count) const
    {
        const MWWorld::LiveCellRef<ESM::Door>* ref = ptr.get<ESM::Door>();

        MWGui::ToolTipInfo info;
        std::string_view name = getName(ptr);
        info.caption = MyGUI::TextIterator::toTagsString(MWGui::toUString(name));

        std::string text;

        if (ptr.getCellRef().getTeleport())
        {
            text += "\n#{sTo}";
            text += "\n" + getDestination(*ref);
        }

        int lockLevel = ptr.getCellRef().getLockLevel();
        if (lockLevel > 0 && lockLevel != ESM::UnbreakableLock)
            text += "\n#{sLockLevel}: " + MWGui::ToolTips::toString(ptr.getCellRef().getLockLevel());
        else if (ptr.getCellRef().getLockLevel() < 0)
            text += "\n#{sUnlocked}";
        if (!ptr.getCellRef().getTrap().empty())
            text += "\n#{sTrapped}";

        if (MWBase::Environment::get().getWindowManager()->getFullHelp())
        {
            text += MWGui::ToolTips::getCellRefString(ptr.getCellRef());
            text += MWGui::ToolTips::getMiscString(ref->mBase->mScript.getRefIdString(), "Script");
        }
        info.text = text;

        return info;
    }

    std::string Door::getDestination(const MWWorld::LiveCellRef<ESM::Door>& door)
    {
        std::string_view dest = door.mRef.getDestCell();
        if (dest.empty())
        {
            // door leads to exterior, use cell name (if any), otherwise translated region name
            auto world = MWBase::Environment::get().getWorld();
            const osg::Vec2i index
                = MWWorld::positionToCellIndex(door.mRef.getDoorDest().pos[0], door.mRef.getDoorDest().pos[1]);
            const ESM::Cell* cell = world->getStore().get<ESM::Cell>().search(index.x(), index.y());
            dest = world->getCellName(cell);
        }

        return "#{sCell=" + std::string{ dest } + "}";
    }

    MWWorld::Ptr Door::copyToCellImpl(const MWWorld::ConstPtr& ptr, MWWorld::CellStore& cell) const
    {
        const MWWorld::LiveCellRef<ESM::Door>* ref = ptr.get<ESM::Door>();

        return MWWorld::Ptr(cell.insert(ref), &cell);
    }

    void Door::ensureCustomData(const MWWorld::Ptr& ptr) const
    {
        if (!ptr.getRefData().getCustomData())
        {
            ptr.getRefData().setCustomData(std::make_unique<DoorCustomData>());
        }
    }

    MWWorld::DoorState Door::getDoorState(const MWWorld::ConstPtr& ptr) const
    {
        if (!ptr.getRefData().getCustomData())
            return MWWorld::DoorState::Idle;
        const DoorCustomData& customData = ptr.getRefData().getCustomData()->asDoorCustomData();
        return customData.mDoorState;
    }

    void Door::setDoorState(const MWWorld::Ptr& ptr, MWWorld::DoorState state) const
    {
        if (ptr.getCellRef().getTeleport())
            throw std::runtime_error("load doors can't be moved");

        ensureCustomData(ptr);
        DoorCustomData& customData = ptr.getRefData().getCustomData()->asDoorCustomData();
        customData.mDoorState = state;
    }

    void Door::readAdditionalState(const MWWorld::Ptr& ptr, const ESM::ObjectState& state) const
    {
        if (!state.mHasCustomState)
            return;

        ensureCustomData(ptr);
        DoorCustomData& customData = ptr.getRefData().getCustomData()->asDoorCustomData();
        const ESM::DoorState& doorState = state.asDoorState();
        customData.mDoorState = MWWorld::DoorState(doorState.mDoorState);
    }

    void Door::writeAdditionalState(const MWWorld::ConstPtr& ptr, ESM::ObjectState& state) const
    {
        if (!ptr.getRefData().getCustomData())
        {
            state.mHasCustomState = false;
            return;
        }

        const DoorCustomData& customData = ptr.getRefData().getCustomData()->asDoorCustomData();
        ESM::DoorState& doorState = state.asDoorState();
        doorState.mDoorState = int(customData.mDoorState);
    }

}
