#include "localscripts.hpp"

#include <components/esm3/loadcell.hpp>
#include <components/misc/strings/lower.hpp>

#include "../mwmechanics/aicombat.hpp"
#include "../mwmechanics/aiescort.hpp"
#include "../mwmechanics/aifollow.hpp"
#include "../mwmechanics/aipackage.hpp"
#include "../mwmechanics/aipursue.hpp"
#include "../mwmechanics/aisequence.hpp"
#include "../mwmechanics/aitravel.hpp"
#include "../mwmechanics/aiwander.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/ptr.hpp"

#include "context.hpp"
#include "worldview.hpp"

namespace MWLua
{
    struct LocalMWScript
    {
        LObject mSelf;
    };
}

namespace sol
{
    template <>
    struct is_automagical<MWBase::LuaManager::ActorControls> : std::false_type
    {
    };
    template <>
    struct is_automagical<MWLua::LocalScripts::SelfObject> : std::false_type
    {
    };
    template <>
    struct is_automagical<MWLua::LocalMWScript> : std::false_type
    {
    };
}

namespace MWLua
{

    void LocalScripts::initializeSelfPackage(const Context& context)
    {
        using ActorControls = MWBase::LuaManager::ActorControls;
        sol::usertype<ActorControls> controls = context.mLua->sol().new_usertype<ActorControls>("ActorControls");

#define CONTROL(TYPE, FIELD)                                                                                           \
    sol::property([](const ActorControls& c) { return c.FIELD; },                                                      \
        [](ActorControls& c, const TYPE& v) {                                                                          \
            c.FIELD = v;                                                                                               \
            c.mChanged = true;                                                                                         \
        })
        controls["movement"] = CONTROL(float, mMovement);
        controls["sideMovement"] = CONTROL(float, mSideMovement);
        controls["pitchChange"] = CONTROL(float, mPitchChange);
        controls["yawChange"] = CONTROL(float, mYawChange);
        controls["run"] = CONTROL(bool, mRun);
        controls["sneak"] = CONTROL(bool, mSneak);
        controls["jump"] = CONTROL(bool, mJump);
        controls["use"] = CONTROL(int, mUse);
#undef CONTROL

        sol::usertype<SelfObject> selfAPI = context.mLua->sol().new_usertype<SelfObject>(
            "SelfObject", sol::base_classes, sol::bases<LObject, Object>());
        selfAPI[sol::meta_function::to_string]
            = [](SelfObject& self) { return "openmw.self[" + self.toString() + "]"; };
        selfAPI["object"] = sol::readonly_property([](SelfObject& self) -> LObject { return LObject(self); });
        selfAPI["controls"] = sol::readonly_property([](SelfObject& self) { return &self.mControls; });
        selfAPI["isActive"] = [](SelfObject& self) { return &self.mIsActive; };
        selfAPI["enableAI"] = [](SelfObject& self, bool v) { self.mControls.mDisableAI = !v; };
        selfAPI["mwscript"] = sol::readonly_property([](SelfObject& self) -> sol::optional<LocalMWScript> {
            if (self.ptr().getRefData().getLocals().getScriptId().empty())
                return sol::nullopt;
            else
                return LocalMWScript{ LObject(self.id()) };
        });

        sol::usertype<LocalMWScript> mwscript = context.mLua->sol().new_usertype<LocalMWScript>("LocalMWScript");
        mwscript[sol::meta_function::to_string] = [](const LocalMWScript& s) {
            return s.mSelf.ptr().getRefData().getLocals().getScriptId().getRefIdString();
        };
        mwscript[sol::meta_function::index] = [](const LocalMWScript& s, std::string_view var) {
            MWScript::Locals& locals = s.mSelf.ptr().getRefData().getLocals();
            return locals.getVarAsDouble(locals.getScriptId(), Misc::StringUtils::lowerCase(var));
        };
        mwscript[sol::meta_function::new_index] = [](const LocalMWScript& s, std::string_view var, double val) {
            MWScript::Locals& locals = s.mSelf.ptr().getRefData().getLocals();
            if (!locals.setVar(locals.getScriptId(), Misc::StringUtils::lowerCase(var), val))
                throw std::runtime_error("No variable \"" + std::string(var) + "\" in mwscript \""
                    + locals.getScriptId().getRefIdString() + "\"");
        };

        using AiPackage = MWMechanics::AiPackage;
        sol::usertype<AiPackage> aiPackage = context.mLua->sol().new_usertype<AiPackage>("AiPackage");
        aiPackage["type"] = sol::readonly_property([](const AiPackage& p) -> std::string_view {
            switch (p.getTypeId())
            {
                case MWMechanics::AiPackageTypeId::Wander:
                    return "Wander";
                case MWMechanics::AiPackageTypeId::Travel:
                    return "Travel";
                case MWMechanics::AiPackageTypeId::Escort:
                    return "Escort";
                case MWMechanics::AiPackageTypeId::Follow:
                    return "Follow";
                case MWMechanics::AiPackageTypeId::Activate:
                    return "Activate";
                case MWMechanics::AiPackageTypeId::Combat:
                    return "Combat";
                case MWMechanics::AiPackageTypeId::Pursue:
                    return "Pursue";
                case MWMechanics::AiPackageTypeId::AvoidDoor:
                    return "AvoidDoor";
                case MWMechanics::AiPackageTypeId::Face:
                    return "Face";
                case MWMechanics::AiPackageTypeId::Breathe:
                    return "Breathe";
                case MWMechanics::AiPackageTypeId::Cast:
                    return "Cast";
                default:
                    return "Unknown";
            }
        });
        aiPackage["target"] = sol::readonly_property([](const AiPackage& p) -> sol::optional<LObject> {
            MWWorld::Ptr target = p.getTarget();
            if (target.isEmpty())
                return sol::nullopt;
            else
                return LObject(getId(target));
        });
        aiPackage["sideWithTarget"] = sol::readonly_property([](const AiPackage& p) { return p.sideWithTarget(); });
        aiPackage["destPosition"] = sol::readonly_property([](const AiPackage& p) { return p.getDestination(); });

        selfAPI["_getActiveAiPackage"] = [](SelfObject& self) -> sol::optional<std::shared_ptr<AiPackage>> {
            const MWWorld::Ptr& ptr = self.ptr();
            MWMechanics::AiSequence& ai = ptr.getClass().getCreatureStats(ptr).getAiSequence();
            if (ai.isEmpty())
                return sol::nullopt;
            else
                return *ai.begin();
        };
        selfAPI["_iterateAndFilterAiSequence"] = [](SelfObject& self, sol::function callback) {
            const MWWorld::Ptr& ptr = self.ptr();
            MWMechanics::AiSequence& ai = ptr.getClass().getCreatureStats(ptr).getAiSequence();

            ai.erasePackagesIf([&](auto& entry) {
                bool keep = LuaUtil::call(callback, entry).template get<bool>();
                return !keep;
            });
        };
        selfAPI["_startAiCombat"] = [](SelfObject& self, const LObject& target) {
            const MWWorld::Ptr& ptr = self.ptr();
            MWMechanics::AiSequence& ai = ptr.getClass().getCreatureStats(ptr).getAiSequence();
            ai.stack(MWMechanics::AiCombat(target.ptr()), ptr);
        };
        selfAPI["_startAiPursue"] = [](SelfObject& self, const LObject& target) {
            const MWWorld::Ptr& ptr = self.ptr();
            MWMechanics::AiSequence& ai = ptr.getClass().getCreatureStats(ptr).getAiSequence();
            ai.stack(MWMechanics::AiPursue(target.ptr()), ptr);
        };
        selfAPI["_startAiFollow"] = [](SelfObject& self, const LObject& target) {
            const MWWorld::Ptr& ptr = self.ptr();
            MWMechanics::AiSequence& ai = ptr.getClass().getCreatureStats(ptr).getAiSequence();
            ai.stack(MWMechanics::AiFollow(target.ptr()), ptr);
        };
        selfAPI["_startAiEscort"] = [](SelfObject& self, const LObject& target, LCell cell, float duration,
                                        const osg::Vec3f& dest) {
            const MWWorld::Ptr& ptr = self.ptr();
            MWMechanics::AiSequence& ai = ptr.getClass().getCreatureStats(ptr).getAiSequence();
            // TODO: change AiEscort implementation to accept ptr instead of a non-unique refId.
            const ESM::RefId& refId = target.ptr().getCellRef().getRefId();
            int gameHoursDuration = static_cast<int>(std::ceil(duration / 3600.0));
            auto* esmCell = cell.mStore->getCell();
            if (esmCell->isExterior())
                ai.stack(MWMechanics::AiEscort(refId, gameHoursDuration, dest.x(), dest.y(), dest.z(), false), ptr);
            else
                ai.stack(MWMechanics::AiEscort(
                             refId, esmCell->getNameId(), gameHoursDuration, dest.x(), dest.y(), dest.z(), false),
                    ptr);
        };
        selfAPI["_startAiWander"] = [](SelfObject& self, int distance, float duration) {
            const MWWorld::Ptr& ptr = self.ptr();
            MWMechanics::AiSequence& ai = ptr.getClass().getCreatureStats(ptr).getAiSequence();
            int gameHoursDuration = static_cast<int>(std::ceil(duration / 3600.0));
            ai.stack(MWMechanics::AiWander(distance, gameHoursDuration, 0, {}, false), ptr);
        };
        selfAPI["_startAiTravel"] = [](SelfObject& self, const osg::Vec3f& target) {
            const MWWorld::Ptr& ptr = self.ptr();
            MWMechanics::AiSequence& ai = ptr.getClass().getCreatureStats(ptr).getAiSequence();
            ai.stack(MWMechanics::AiTravel(target.x(), target.y(), target.z(), false), ptr);
        };
    }

    LocalScripts::LocalScripts(LuaUtil::LuaState* lua, const LObject& obj)
        : LuaUtil::ScriptsContainer(lua, "L" + idToString(obj.id()))
        , mData(obj)
    {
        this->addPackage("openmw.self", sol::make_object(lua->sol(), &mData));
        registerEngineHandlers(
            { &mOnActiveHandlers, &mOnInactiveHandlers, &mOnConsumeHandlers, &mOnActivatedHandlers });
    }

    void LocalScripts::receiveEngineEvent(const EngineEvent& event)
    {
        std::visit(
            [this](auto&& arg) {
                using EventT = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<EventT, OnActive>)
                {
                    mData.mIsActive = true;
                    callEngineHandlers(mOnActiveHandlers);
                }
                else if constexpr (std::is_same_v<EventT, OnInactive>)
                {
                    mData.mIsActive = false;
                    callEngineHandlers(mOnInactiveHandlers);
                }
                else if constexpr (std::is_same_v<EventT, OnActivated>)
                {
                    callEngineHandlers(mOnActivatedHandlers, arg.mActivatingActor);
                }
                else
                {
                    static_assert(std::is_same_v<EventT, OnConsume>);
                    callEngineHandlers(mOnConsumeHandlers, arg.mConsumable);
                }
            },
            event);
    }

    void LocalScripts::applyStatsCache()
    {
        const auto& ptr = mData.ptr();
        for (auto& [stat, value] : mData.mStatsCache)
            stat(ptr, value);
        mData.mStatsCache.clear();
    }
}
