#include "types.hpp"

#include <components/esm3/loadspel.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/activespells.hpp>
#include <components/lua/luastate.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/resourcesystem.hpp>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>

#include "../luabindings.hpp"
#include "../worldview.hpp"

namespace sol
{
    template <>
    struct is_automagical<ESM::Spell> : std::false_type
    {
    };
}

namespace MWLua
{

    void addSpellBindings(sol::table spell, const Context& context)
    {
        const MWWorld::Store<ESM::Spell>* store = &MWBase::Environment::get().getWorld()->getStore().get<ESM::Spell>();
        spell["record"]
            = sol::overload([](const Object& obj) -> const ESM::Spell* { return obj.ptr().get<ESM::Spell>()->mBase; },
                [store](const std::string& recordId) -> const ESM::Spell* {
                    return store->find(ESM::RefId::stringRefId(recordId));
                });

        sol::usertype<ESM::Spell> record = context.mLua->sol().new_usertype<ESM::Spell>("ESM3_Spell");
        sol::usertype<ESM::ActiveSpells::ActiveSpellParams> activeSpellType = context.mLua->sol().new_usertype<ESM::ActiveSpells::ActiveSpellParams>("ESM3_ActiveSpell");

        record[sol::meta_function::to_string]
            = [](const ESM::Spell& rec) -> std::string { return "ESM3_Spell[" + rec.mId.getRefIdString() + "]"; };

        record["id"]
            = sol::readonly_property([](const ESM::Spell& rec) -> std::string { return rec.mId.getRefIdString(); });
        record["name"] = sol::readonly_property([](const ESM::Spell& rec) -> std::string { return rec.mName; });

        record["effects"] = sol::readonly_property([context](const ESM::Spell& rec) -> sol::table {
            auto outertab = context.mLua->sol().create_table();
            for(int i=0; i<rec.mEffects.mList.size(); ++i) {
                sol::table innertab = context.mLua->sol().create_table();
                const ESM::ENAMstruct& eff = rec.mEffects.mList[i];
                innertab["id"] = eff.mEffectID;
                innertab["skill"] = eff.mSkill;
                innertab["attribute"] = eff.mAttribute;
                innertab["range"] = eff.mRange;
                innertab["area"] = eff.mArea;
                innertab["duration"] = eff.mDuration;
                innertab["magnMin"] = eff.mMagnMin;
                innertab["magnMax"] = eff.mMagnMax;
                outertab[i+1] = innertab;
            }
            return outertab;
        });
    }
}
