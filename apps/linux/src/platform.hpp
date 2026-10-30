#pragma once

#include <gtk/gtk.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The few places the app touches the desktop outside GTK widgets: the user's folders, the file dialogs, the
// clipboard, the browser, and keeping the display awake.
namespace kl::platform {

std::filesystem::path HomeFolder();        // the user's home folder, which holds KettleLogic/Recipes
std::filesystem::path DataFolder();        // $XDG_DATA_HOME/KettleLogic: settings and the user's libraries
std::filesystem::path HelpFolder();        // the folder that holds the user guide, or an empty path when it is missing

struct FileType {
    std::string name;                      // such as "BeerJSON recipes"
    std::vector<std::string> patterns;     // such as {"*.json"}
};

// The standard open and save dialogs. They start in `startFolder` when it exists. The callback gets nothing when the
// dialog is canceled.
using PathCallback = std::function<void(std::optional<std::filesystem::path>)>;
void OpenFile(GtkWindow* parent, const std::filesystem::path& startFolder, const std::vector<FileType>& types,
              const std::string& title, PathCallback done);
void SaveFile(GtkWindow* parent, const std::filesystem::path& startFolder, const std::string& suggestedName,
              const std::vector<FileType>& types, PathCallback done);
void ChooseFolder(GtkWindow* parent, const std::filesystem::path& startFolder, const std::string& title,
                  PathCallback done);

// Reads and writes a whole UTF-8 text file. They throw std::runtime_error with a message the user can read.
std::string ReadTextFile(const std::filesystem::path& file);
void WriteTextFile(const std::filesystem::path& file, const std::string& utf8);
void WriteBinaryFile(const std::filesystem::path& file, const std::string& bytes);

void CopyText(std::string_view utf8);
void OpenUrl(GtkWindow* parent, std::string_view url);

// Keeps the display on while it is held, as the brew-day window does.
class DisplayLock {
public:
    ~DisplayLock() { Set(nullptr, false); }
    void Set(GtkWindow* window, bool on);

private:
    GtkApplication* application_ = nullptr;
    guint cookie_ = 0;
};

}  // namespace kl::platform
