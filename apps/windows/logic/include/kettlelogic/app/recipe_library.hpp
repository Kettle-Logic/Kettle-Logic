#pragma once

#include "kettlelogic/app/preferences.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace kettlelogic::app {

// The folder where new recipes are saved: `KettleLogic\Recipes` in the user's home folder unless the user chose
// another one in the settings. It is created the first time it is needed. A document saved there is named after
// its first recipe, so renaming the recipe renames the file.
class RecipeLibrary {
public:
    RecipeLibrary(const Preferences& preferences, std::filesystem::path home);

    std::filesystem::path folder() const;
    // `<home>\KettleLogic\Recipes`, the folder used until the user chooses another.
    std::filesystem::path defaultFolder() const;
    // The folder, created if it does not exist yet.
    std::filesystem::path ensureFolder() const;

    // The recipe files in the folder, by name. A sidecar or a backup copy is not a recipe of its own. The folder
    // is not created here, so a missing folder has no files.
    std::vector<std::filesystem::path> recipeFiles() const;

    // Whether a file is in the recipe folder, so the app may rename and remove it.
    bool contains(const std::filesystem::path& file) const;

    // A file name for a recipe called `name` that is not already taken: "Pale Ale.json", then "Pale Ale 2.json",
    // and so on. `current` is the file being renamed, if any.
    std::filesystem::path fileFor(const std::string& name, const std::filesystem::path& current = {}) const;

    // The name a recipe's file is given, without its extension. Characters that cannot be in a file name are
    // replaced, and a blank name becomes "Recipe".
    static std::string safeFileName(const std::string& name);

    // Removes a document's BeerJSON file and its sidecar.
    static void removeFiles(const std::filesystem::path& file);

private:
    const Preferences& preferences_;
    std::filesystem::path home_;
};

// Orders names the way a file list does: letters without regard to case, and runs of digits as numbers.
bool naturalLess(const std::string& a, const std::string& b);

}  // namespace kettlelogic::app
