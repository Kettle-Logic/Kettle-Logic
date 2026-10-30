#include "kettlelogic/model/model.hpp"

namespace kettlelogic::model {

std::string_view recipeTypeName(RecipeType type) {
    switch (type) {
        case RecipeType::Cider: return "cider";
        case RecipeType::Kombucha: return "kombucha";
        case RecipeType::Soda: return "soda";
        case RecipeType::Other: return "other";
        case RecipeType::Mead: return "mead";
        case RecipeType::Wine: return "wine";
        case RecipeType::Extract: return "extract";
        case RecipeType::PartialMash: return "partial mash";
        case RecipeType::AllGrain: return "all grain";
    }
    return "";
}

std::optional<RecipeType> recipeTypeFromName(std::string_view name) {
    for (const RecipeType type : {RecipeType::Cider, RecipeType::Kombucha, RecipeType::Soda, RecipeType::Other,
                                  RecipeType::Mead, RecipeType::Wine, RecipeType::Extract, RecipeType::PartialMash,
                                  RecipeType::AllGrain}) {
        if (recipeTypeName(type) == name) {
            return type;
        }
    }
    return std::nullopt;
}

Recipe* Document::findRecipe(EntityId id) {
    for (Recipe& r : recipes) {
        if (r.id == id) {
            return &r;
        }
    }
    return nullptr;
}

const Recipe* Document::findRecipe(EntityId id) const {
    for (const Recipe& r : recipes) {
        if (r.id == id) {
            return &r;
        }
    }
    return nullptr;
}

styles::UserStyle* Document::findUserStyle(std::string_view id) {
    for (styles::UserStyle& style : userStyles) {
        if (style.id == id) {
            return &style;
        }
    }
    return nullptr;
}

const styles::UserStyle* Document::findUserStyle(std::string_view id) const {
    for (const styles::UserStyle& style : userStyles) {
        if (style.id == id) {
            return &style;
        }
    }
    return nullptr;
}

Folder* Document::findFolder(EntityId id) {
    for (Folder& f : folders) {
        if (f.id == id) {
            return &f;
        }
    }
    return nullptr;
}

const Folder* Document::findFolder(EntityId id) const {
    for (const Folder& f : folders) {
        if (f.id == id) {
            return &f;
        }
    }
    return nullptr;
}

Folder* Document::trashFolder() {
    for (Folder& f : folders) {
        if (f.trash) {
            return &f;
        }
    }
    return nullptr;
}

const Folder* Document::trashFolder() const {
    for (const Folder& f : folders) {
        if (f.trash) {
            return &f;
        }
    }
    return nullptr;
}

}  // namespace kettlelogic::model
