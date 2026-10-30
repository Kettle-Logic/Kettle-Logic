#pragma once

#include "kettlelogic/model/model.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace kettlelogic::model {

// Calls fn(kind, pointer, name, uuid) for every entity in the document, in
// document order. `pointer` is the JSON Pointer the entity has in the BeerJSON
// output, so the same walk serves the writer's layout, the reader, and the
// sidecar. Works on const and non-const documents.
template <typename DocumentT, typename Fn>
void forEachEntity(DocumentT& document, Fn&& fn) {
    for (std::size_t r = 0; r < document.recipes.size(); ++r) {
        auto& recipe = document.recipes[r];
        const std::string recipePointer = "/beerjson/recipes/" + std::to_string(r);
        fn(std::string_view{"recipe"}, recipePointer, recipe.name, recipe.uuid);

        const std::string ingredients = recipePointer + "/ingredients";
        const auto at = [&](const std::string& list, std::size_t i) {
            return ingredients + "/" + list + "/" + std::to_string(i);
        };
        for (std::size_t i = 0; i < recipe.fermentables.size(); ++i) {
            auto& x = recipe.fermentables[i];
            fn(std::string_view{"fermentable"}, at("fermentable_additions", i), x.name, x.uuid);
        }
        if (recipe.hops) {
            for (std::size_t i = 0; i < recipe.hops->size(); ++i) {
                auto& x = (*recipe.hops)[i];
                fn(std::string_view{"hop"}, at("hop_additions", i), x.name, x.uuid);
            }
        }
        if (recipe.miscellaneous) {
            for (std::size_t i = 0; i < recipe.miscellaneous->size(); ++i) {
                auto& x = (*recipe.miscellaneous)[i];
                fn(std::string_view{"miscellaneous"}, at("miscellaneous_additions", i), x.name, x.uuid);
            }
        }
        if (recipe.cultures) {
            for (std::size_t i = 0; i < recipe.cultures->size(); ++i) {
                auto& x = (*recipe.cultures)[i];
                fn(std::string_view{"culture"}, at("culture_additions", i), x.name, x.uuid);
            }
        }
        if (recipe.waters) {
            for (std::size_t i = 0; i < recipe.waters->size(); ++i) {
                auto& x = (*recipe.waters)[i];
                fn(std::string_view{"water"}, at("water_additions", i), x.water.name, x.uuid);
            }
        }
    }
}

}  // namespace kettlelogic::model
