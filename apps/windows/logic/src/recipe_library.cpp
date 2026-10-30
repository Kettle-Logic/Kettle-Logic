#include "kettlelogic/app/recipe_library.hpp"

#include "kettlelogic/app/text.hpp"

#include <algorithm>
#include <cctype>

namespace kettlelogic::app {

namespace fs = std::filesystem;

RecipeLibrary::RecipeLibrary(const Preferences& preferences, fs::path home)
    : preferences_(preferences), home_(std::move(home)) {}

fs::path RecipeLibrary::defaultFolder() const {
    return home_ / "KettleLogic" / "Recipes";
}

fs::path RecipeLibrary::folder() const {
    const std::string chosen = preferences_.string(PrefKey::recipeFolder);
    return chosen.empty() ? defaultFolder() : fs::path(std::u8string(chosen.begin(), chosen.end()));
}

fs::path RecipeLibrary::ensureFolder() const {
    const fs::path path = folder();
    fs::create_directories(path);
    return path;
}

bool naturalLess(const std::string& a, const std::string& b) {
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.size() && j < b.size()) {
        if (std::isdigit(static_cast<unsigned char>(a[i])) && std::isdigit(static_cast<unsigned char>(b[j]))) {
            std::size_t ei = i;
            std::size_t ej = j;
            while (ei < a.size() && std::isdigit(static_cast<unsigned char>(a[ei]))) ++ei;
            while (ej < b.size() && std::isdigit(static_cast<unsigned char>(b[ej]))) ++ej;
            std::string da = a.substr(i, ei - i);
            std::string db = b.substr(j, ej - j);
            da.erase(0, std::min(da.find_first_not_of('0'), da.size()));
            db.erase(0, std::min(db.find_first_not_of('0'), db.size()));
            if (da.size() != db.size()) {
                return da.size() < db.size();
            }
            if (da != db) {
                return da < db;
            }
            i = ei;
            j = ej;
        } else {
            const int ca = std::tolower(static_cast<unsigned char>(a[i]));
            const int cb = std::tolower(static_cast<unsigned char>(b[j]));
            if (ca != cb) {
                return ca < cb;
            }
            ++i;
            ++j;
        }
    }
    return a.size() - i < b.size() - j;
}

std::vector<fs::path> RecipeLibrary::recipeFiles() const {
    static const char* const companions[] = {"kettlelogic.json", "previous.json", "autosave.json"};
    std::vector<fs::path> files;
    std::error_code error;
    for (fs::directory_iterator it(folder(), error), end; !error && it != end; it.increment(error)) {
        const fs::path& path = it->path();
        if (path.extension() != ".json" || !it->is_regular_file(error)) {
            continue;
        }
        const std::string name = path.filename().string();
        const bool companion = std::any_of(std::begin(companions), std::end(companions), [&](const char* suffix) {
            const std::string tail = std::string(".") + suffix;
            return name.size() >= tail.size() && name.compare(name.size() - tail.size(), tail.size(), tail) == 0;
        });
        if (!companion) {
            files.push_back(path);
        }
    }
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
        // By name without the extension, so "Pale Ale" comes before "Pale Ale 2", then "Pale Ale 10".
        return naturalLess(a.stem().string(), b.stem().string());
    });
    return files;
}

bool RecipeLibrary::contains(const fs::path& file) const {
    std::error_code error;
    const fs::path parent = fs::weakly_canonical(file.parent_path(), error);
    const fs::path own = fs::weakly_canonical(folder(), error);
    return parent == own;
}

fs::path RecipeLibrary::fileFor(const std::string& name, const fs::path& current) const {
    const fs::path directory = ensureFolder();
    const std::string base = safeFileName(name);
    const auto candidateFor = [&](int number) {
        const std::string leaf = number == 1 ? base : base + " " + std::to_string(number);
        const std::u8string utf8(leaf.begin(), leaf.end());
        return directory / fs::path(utf8 + u8".json");
    };
    int number = 1;
    fs::path candidate = candidateFor(number);
    while (fs::exists(candidate) && candidate != current) {
        candidate = candidateFor(++number);
    }
    return candidate;
}

std::string RecipeLibrary::safeFileName(const std::string& name) {
    // Slash, colon, and backslash are replaced as on the Mac. The other characters Windows does not allow
    // in a file name are replaced too.
    std::string cleaned = name;
    for (char& c : cleaned) {
        if (std::string_view("/:\\*?\"<>|").find(c) != std::string_view::npos || static_cast<unsigned char>(c) < 0x20) {
            c = '-';
        }
    }
    cleaned = trimmed(cleaned);
    // Windows drops a trailing dot or space, so a name that ends in one would not match its file.
    while (!cleaned.empty() && (cleaned.back() == '.' || cleaned.back() == ' ')) {
        cleaned.pop_back();
    }
    return cleaned.empty() ? "Recipe" : cleaned;
}

void RecipeLibrary::removeFiles(const fs::path& file) {
    const fs::path sidecar = file.parent_path() / (file.stem().native() + fs::path(".kettlelogic.json").native());
    std::error_code ignored;
    fs::remove(file, ignored);
    fs::remove(sidecar, ignored);
}

}  // namespace kettlelogic::app
