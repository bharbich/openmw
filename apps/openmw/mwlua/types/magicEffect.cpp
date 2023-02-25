#include "types.hpp"

#include <components/esm3/loadmgef.hpp>
#include <components/lua/luastate.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/esm3/loadspel.hpp>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwlua/context.hpp>

#include "../luabindings.hpp"
#include "../worldview.hpp"

namespace sol
{
    template <>
    struct is_automagical<ESM::MagicEffect> : std::false_type
    {
    };
}

namespace MWLua
{
    void addMagicEffectBindings(sol::table effect, const Context& context)
    {
        const MWWorld::Store<ESM::MagicEffect>* store = &MWBase::Environment::get().getWorld()->getStore().get<ESM::MagicEffect>();
        store->find(
        effect["record"]
            = sol::overload([](const Object& obj) -> const ESM::MagicEffect* { return obj.ptr().get<ESM::MagicEffect>()->mBase; },
                [store](const int recordId) -> const ESM::MagicEffect* {
                    return store->find(recordId);
                }));

        sol::usertype<ESM::MagicEffect> recordMagicEffect = context.mLua->sol().new_usertype<ESM::MagicEffect>("ESM3_MagicEffect");

        recordMagicEffect[sol::meta_function::to_string]
            = [](const ESM::MagicEffect& rec) -> std::string { return "ESM3_MagicEffect[" + rec.mId.getRefIdString() + "]"; };

        recordMagicEffect["id"]
            = sol::readonly_property([](const ESM::MagicEffect& rec) -> std::string { return rec.mId.getRefIdString(); });

        recordMagicEffect["index"]
            = sol::readonly_property([](const ESM::MagicEffect& rec) -> int { return rec.mIndex; });

        recordMagicEffect["name"] = sol::readonly_property([](const ESM::MagicEffect& rec) -> std::string {
            return ESM::MagicEffect::effectIdToString(std::stoi(rec.mId.getRefIdString()));
        });

        recordMagicEffect["school"] = sol::readonly_property([](const ESM::MagicEffect& rec) -> int { return rec.mData.mSchool; });

        recordMagicEffect["cost"] = sol::readonly_property([](const ESM::MagicEffect& rec) -> float { return rec.mData.mBaseCost; });


        recordMagicEffect["color"] = sol::readonly_property([](const ESM::MagicEffect& rec) -> std::vector<int> {
            return std::vector<int> { rec.mData.mRed, rec.mData.mGreen, rec.mData.mBlue };
        });
        recordMagicEffect["speed"] = sol::readonly_property([](const ESM::MagicEffect& rec) -> float { return rec.mData.mSpeed; });
        
        recordMagicEffect["flags"] = sol::readonly_property([](const ESM::MagicEffect& rec) -> int { return rec.mData.mFlags; });

        recordMagicEffect["harmful"] = sol::readonly_property([](const ESM::MagicEffect& rec) -> bool { 
            return rec.mData.mFlags & ESM::MagicEffect::Harmful;
        });
    }
}
