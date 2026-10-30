#pragma once

#include "pch.h"

// The few places the app touches Windows outside XAML: text encoding, the user's folders, the file dialogs, the
// clipboard, the browser, the display, and the share sheet.
namespace kl::platform {

// UTF-8 text, as the engine and the app's logic use, and the UTF-16 text XAML uses.
winrt::hstring H(std::string_view utf8);
std::string U(winrt::hstring const& text);
std::string U(std::wstring_view text);

std::filesystem::path HomeFolder();          // the user's profile folder, which holds KettleLogic\Recipes
std::filesystem::path DataFolder();          // %LOCALAPPDATA%\KettleLogic: settings and the user's libraries
std::filesystem::path ExecutableFolder();    // where the app is installed, which holds the Help folder

HWND WindowHandle(winrt::Microsoft::UI::Xaml::Window const& window);

struct FileType {
    std::wstring name;     // such as "BeerJSON recipes"
    std::wstring pattern;  // such as "*.json"
};

// The standard open and save dialogs. They start in `startFolder` when it exists, and return nothing when canceled.
std::optional<std::filesystem::path> OpenFile(HWND owner, const std::filesystem::path& startFolder,
                                              const std::vector<FileType>& types, const std::wstring& title);
std::optional<std::filesystem::path> SaveFile(HWND owner, const std::filesystem::path& startFolder,
                                              const std::wstring& suggestedName, const std::vector<FileType>& types,
                                              const std::wstring& defaultExtension);
std::optional<std::filesystem::path> ChooseFolder(HWND owner, const std::filesystem::path& startFolder,
                                                  const std::wstring& title);

// Reads and writes a whole UTF-8 text file. They throw std::runtime_error with a message the user can read.
std::string ReadTextFile(const std::filesystem::path& file);
void WriteTextFile(const std::filesystem::path& file, const std::string& utf8);
void WriteBinaryFile(const std::filesystem::path& file, const std::string& bytes);

void CopyText(std::string_view utf8);
void OpenUrl(std::string_view url);

// Shows the Windows share sheet for a file.
void ShareFile(HWND owner, const std::filesystem::path& file, std::string_view title);

// Keeps the display on while it is alive, as the brew-day window does.
class DisplayLock {
public:
    void Set(bool on);
    ~DisplayLock() { Set(false); }

private:
    winrt::Windows::System::Display::DisplayRequest request_{nullptr};
};

}  // namespace kl::platform
