#include "pch.h"

#include "Platform.h"

#include <fstream>
#include <sstream>

namespace kl::platform {

namespace fs = std::filesystem;

winrt::hstring H(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int size = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), size);
    return winrt::hstring(wide);
}

std::string U(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);
    return utf8;
}

std::string U(winrt::hstring const& text) {
    return U(std::wstring_view(text.c_str(), text.size()));
}

namespace {

fs::path KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    fs::path result;
    if (SUCCEEDED(::SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw)) && raw != nullptr) {
        result = fs::path(raw);
    }
    ::CoTaskMemFree(raw);
    return result;
}

}  // namespace

fs::path HomeFolder() {
    const fs::path home = KnownFolder(FOLDERID_Profile);
    return home.empty() ? fs::temp_directory_path() : home;
}

fs::path DataFolder() {
    const fs::path local = KnownFolder(FOLDERID_LocalAppData);
    return (local.empty() ? fs::temp_directory_path() : local) / L"KettleLogic";
}

fs::path ExecutableFolder() {
    wchar_t buffer[MAX_PATH * 2] = {};
    const DWORD length = ::GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    return fs::path(std::wstring(buffer, length)).parent_path();
}

HWND WindowHandle(winrt::Microsoft::UI::Xaml::Window const& window) {
    HWND handle = nullptr;
    winrt::check_hresult(window.as<::IWindowNative>()->get_WindowHandle(&handle));
    return handle;
}

// MARK: Dialogs

namespace {

void SetStartFolder(IFileDialog* dialog, const fs::path& folder) {
    std::error_code error;
    if (folder.empty() || !fs::is_directory(folder, error)) {
        return;
    }
    winrt::com_ptr<IShellItem> item;
    if (SUCCEEDED(::SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(item.put())))) {
        dialog->SetFolder(item.get());
    }
}

void SetTypes(IFileDialog* dialog, const std::vector<FileType>& types) {
    if (types.empty()) {
        return;
    }
    std::vector<COMDLG_FILTERSPEC> specs;
    for (const FileType& type : types) {
        specs.push_back(COMDLG_FILTERSPEC{type.name.c_str(), type.pattern.c_str()});
    }
    dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
    dialog->SetFileTypeIndex(1);
}

std::optional<fs::path> ResultPath(IFileDialog* dialog) {
    winrt::com_ptr<IShellItem> result;
    if (FAILED(dialog->GetResult(result.put()))) {
        return std::nullopt;
    }
    PWSTR raw = nullptr;
    if (FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &raw)) || raw == nullptr) {
        return std::nullopt;
    }
    fs::path path(raw);
    ::CoTaskMemFree(raw);
    return path;
}

}  // namespace

std::optional<fs::path> OpenFile(HWND owner, const fs::path& startFolder, const std::vector<FileType>& types,
                                 const std::wstring& title) {
    winrt::com_ptr<IFileOpenDialog> dialog = winrt::create_instance<IFileOpenDialog>(CLSID_FileOpenDialog);
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
    SetTypes(dialog.get(), types);
    SetStartFolder(dialog.get(), startFolder);
    if (!title.empty()) {
        dialog->SetTitle(title.c_str());
    }
    if (FAILED(dialog->Show(owner))) {
        return std::nullopt;  // canceled
    }
    return ResultPath(dialog.get());
}

std::optional<fs::path> SaveFile(HWND owner, const fs::path& startFolder, const std::wstring& suggestedName,
                                 const std::vector<FileType>& types, const std::wstring& defaultExtension) {
    winrt::com_ptr<IFileSaveDialog> dialog = winrt::create_instance<IFileSaveDialog>(CLSID_FileSaveDialog);
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT);
    SetTypes(dialog.get(), types);
    SetStartFolder(dialog.get(), startFolder);
    dialog->SetFileName(suggestedName.c_str());
    if (!defaultExtension.empty()) {
        dialog->SetDefaultExtension(defaultExtension.c_str());
    }
    if (FAILED(dialog->Show(owner))) {
        return std::nullopt;
    }
    return ResultPath(dialog.get());
}

std::optional<fs::path> ChooseFolder(HWND owner, const fs::path& startFolder, const std::wstring& title) {
    winrt::com_ptr<IFileOpenDialog> dialog = winrt::create_instance<IFileOpenDialog>(CLSID_FileOpenDialog);
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    SetStartFolder(dialog.get(), startFolder);
    if (!title.empty()) {
        dialog->SetTitle(title.c_str());
    }
    if (FAILED(dialog->Show(owner))) {
        return std::nullopt;
    }
    return ResultPath(dialog.get());
}

// MARK: Files

std::string ReadTextFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw std::runtime_error("The file \"" + file.filename().string() + "\" could not be opened.");
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string text = buffer.str();
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        text.erase(0, 3);  // a byte order mark
    }
    return text;
}

void WriteBinaryFile(const fs::path& file, const std::string& bytes) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) {
        throw std::runtime_error("The file \"" + file.filename().string() + "\" could not be written.");
    }
}

void WriteTextFile(const fs::path& file, const std::string& utf8) {
    WriteBinaryFile(file, utf8);
}

// MARK: Shell

void CopyText(std::string_view utf8) {
    winrt::Windows::ApplicationModel::DataTransfer::DataPackage package;
    package.SetText(H(utf8));
    winrt::Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);
}

void OpenUrl(std::string_view url) {
    winrt::Windows::System::Launcher::LaunchUriAsync(winrt::Windows::Foundation::Uri(H(url)));
}

void ShareFile(HWND owner, const fs::path& file, std::string_view title) {
    using namespace winrt::Windows::ApplicationModel::DataTransfer;
    auto interop = winrt::get_activation_factory<DataTransferManager, IDataTransferManagerInterop>();
    DataTransferManager manager{nullptr};
    winrt::check_hresult(interop->GetForWindow(owner, winrt::guid_of<DataTransferManager>(), winrt::put_abi(manager)));

    const winrt::hstring shareTitle = H(title);
    const winrt::hstring path = winrt::hstring(file.c_str());
    manager.DataRequested([shareTitle, path](DataTransferManager const&, DataRequestedEventArgs const& args) {
        DataRequest request = args.Request();
        request.Data().Properties().Title(shareTitle);
        // The file is read after the request returns, so the data package holds a deferral.
        DataRequestDeferral deferral = request.GetDeferral();
        auto operation = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(path);
        operation.Completed([request, deferral](auto const& done, auto) {
            try {
                request.Data().SetStorageItems(
                    winrt::single_threaded_vector<winrt::Windows::Storage::IStorageItem>(
                        {done.GetResults().template as<winrt::Windows::Storage::IStorageItem>()}));
            } catch (winrt::hresult_error const&) {
                request.FailWithDisplayText(L"The file could not be shared.");
            }
            deferral.Complete();
        });
    });
    winrt::check_hresult(interop->ShowShareUIForWindow(owner));
}

void DisplayLock::Set(bool on) {
    try {
        if (on && !request_) {
            request_ = winrt::Windows::System::Display::DisplayRequest();
            request_.RequestActive();
        } else if (!on && request_) {
            request_.RequestRelease();
            request_ = nullptr;
        }
    } catch (winrt::hresult_error const&) {
        request_ = nullptr;
    }
}

}  // namespace kl::platform
