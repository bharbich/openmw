#include "types.hpp"

#include <components/esm3/loadingr.hpp>
#include <components/lua/luastate.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/resourcesystem.hpp>

#include <apps/openmw/mwworld/esmstore.hpp>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>

namespace sol
{
    template <>
    struct is_automagical<ESM::Ingredient> : std::false_type
    {
    };
}

namespace MWLua
{
    void addIngredientBindings(sol::table ingredient, const Context& context)
    {
        auto vfs = MWBase::Environment::get().getResourceSystem()->getVFS();

        const MWWorld::Store<ESM::Ingredient>* store
            = &MWBase::Environment::get().getWorld()->getStore().get<ESM::Ingredient>();
        ingredient["record"] = sol::overload(
            [](const Object& obj) -> const ESM::Ingredient* { return obj.ptr().get<ESM::Ingredient>()->mBase; },
            [store](const std::string& recordID) -> const ESM::Ingredient* {
                return store->find(ESM::RefId::stringRefId(recordID));
            });
        sol::usertype<ESM::Ingredient> record = context.mLua->sol().new_usertype<ESM::Ingredient>(("ESM3_Ingredient"));
        record[sol::meta_function::to_string]
            = [](const ESM::Ingredient& rec) { return "ESM3_Ingredient[" + rec.mId.getRefIdString() + "]"; };
        record["id"] = sol::readonly_property(
            [](const ESM::Ingredient& rec) -> std::string { return rec.mId.getRefIdString(); });
        record["name"] = sol::readonly_property([](const ESM::Ingredient& rec) -> std::string { return rec.mName; });
        record["model"] = sol::readonly_property([vfs](const ESM::Ingredient& rec) -> std::string {
            return Misc::ResourceHelpers::correctMeshPath(rec.mModel, vfs);
        });
        record["mwscript"] = sol::readonly_property(
            [](const ESM::Ingredient& rec) -> std::string { return rec.mScript.getRefIdString(); });
        record["icon"] = sol::readonly_property([vfs](const ESM::Ingredient& rec) -> std::string {
            return Misc::ResourceHelpers::correctIconPath(rec.mIcon, vfs);
        });
        record["weight"]
            = sol::readonly_property([](const ESM::Ingredient& rec) -> float { return rec.mData.mWeight; });
        record["value"] = sol::readonly_property([](const ESM::Ingredient& rec) -> int { return rec.mData.mValue; });
    }
}
