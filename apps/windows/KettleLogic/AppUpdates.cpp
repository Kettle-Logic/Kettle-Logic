#include "pch.h"

#include "AppUpdates.h"

#include "Platform.h"

#include <winhttp.h>

#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace kl::update_check {

namespace {

namespace updates = kettlelogic::app::updates;
namespace fs = std::filesystem;

std::wstring Wide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int size = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), size);
    return wide;
}

// Closes a WinHTTP handle when it goes out of scope.
struct HttpHandle {
    HINTERNET handle = nullptr;
    ~HttpHandle() {
        if (handle != nullptr) WinHttpCloseHandle(handle);
    }
};

// Sends a GET to `url` and passes each part of the body to `sink`. WinHTTP follows redirects, which GitHub's download
// links use. Throws std::runtime_error when the request fails or the answer is not 200.
void Fetch(const std::string& url, const std::function<void(const char*, DWORD)>& sink) {
    const std::wstring wideUrl = Wide(url);
    wchar_t host[256];
    wchar_t path[2048];
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof parts;
    parts.lpszHostName = host;
    parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts)) {
        throw std::runtime_error("the update address is not valid");
    }
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;

    const std::wstring agent = L"KettleLogic/" + Wide(updates::kAppVersion);
    HttpHandle session{WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0)};
    if (session.handle == nullptr) throw std::runtime_error("the network could not be opened");
    WinHttpSetTimeouts(session.handle, 15000, 15000, 15000, 60000);

    HttpHandle connection{WinHttpConnect(session.handle, host, parts.nPort, 0)};
    if (connection.handle == nullptr) throw std::runtime_error("the update server could not be reached");

    HttpHandle request{WinHttpOpenRequest(connection.handle, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0)};
    if (request.handle == nullptr) throw std::runtime_error("the update request could not be made");
    if (!WinHttpSendRequest(request.handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.handle, nullptr)) {
        throw std::runtime_error("the update server did not answer");
    }

    DWORD status = 0;
    DWORD statusSize = sizeof status;
    if (!WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) ||
        status != 200) {
        throw std::runtime_error("the update server answered with status " + std::to_string(status));
    }

    std::vector<char> buffer(64 * 1024);
    while (true) {
        DWORD read = 0;
        if (!WinHttpReadData(request.handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            throw std::runtime_error("the download was interrupted");
        }
        if (read == 0) break;
        sink(buffer.data(), read);
    }
}

// The update list, fetched through WinHTTP.
class WinHttpTransport : public updates::Transport {
public:
    std::string get(const std::string& url) override {
        std::string body;
        Fetch(url, [&body](const char* data, DWORD size) { body.append(data, size); });
        return body;
    }
};

// Downloads the installer to `file`, and returns it once its SHA-256 matches the list. A file that does not match is
// deleted.
fs::path Download(const updates::Release& release, const fs::path& folder) {
    std::wstring name = fs::path(Wide(release.url)).filename().wstring();
    if (name.empty()) name = L"KettleLogic-Setup.exe";
    fs::create_directories(folder);
    const fs::path file = folder / name;
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("the installer could not be saved in " + folder.string());
        Fetch(release.url, [&out](const char* data, DWORD size) { out.write(data, size); });
        if (!out) throw std::runtime_error("the installer could not be saved");
    }

    std::ifstream in(file, std::ios::binary);
    std::stringstream bytes;
    bytes << in.rdbuf();
    if (!updates::matchesSha256(bytes.str(), release.sha256)) {
        std::error_code ignored;
        fs::remove(file, ignored);
        throw std::runtime_error("the installer does not match the checksum in the update list, so it was not run");
    }
    return file;
}

// Runs a command with no window, and returns what it printed.
std::string RunCapture(std::wstring commandLine) {
    SECURITY_ATTRIBUTES security{sizeof security, nullptr, TRUE};
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) throw std::runtime_error("the signature could not be read");
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                        nullptr, nullptr, &startup, &process);
    CloseHandle(writePipe);
    if (!started) {
        CloseHandle(readPipe);
        throw std::runtime_error("PowerShell could not be started to check the installer's signature");
    }

    std::string output;
    char buffer[1024];
    DWORD read = 0;
    while (ReadFile(readPipe, buffer, sizeof buffer, &read, nullptr) && read > 0) {
        output.append(buffer, read);
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    CloseHandle(readPipe);
    return output;
}

// The certificate thumbprint of a file whose Authenticode signature is valid, or nothing if it is not signed or the
// signature does not hold.
std::string ThumbprintOf(const fs::path& file) {
    std::wstring quoted;
    for (wchar_t c : file.wstring()) {
        quoted += c;
        if (c == L'\'') quoted += L'\'';
    }
    const std::wstring script =
        L"$s = Get-AuthenticodeSignature -LiteralPath '" + quoted +
        L"'; if ($s.Status -eq 'Valid') { $s.SignerCertificate.Thumbprint }";
    std::string output = RunCapture(L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"" +
                                    script + L"\"");
    while (!output.empty() && (output.back() == '\r' || output.back() == '\n' || output.back() == ' ')) {
        output.pop_back();
    }
    return output;
}

// Whether the installer is signed by the same certificate as this app. A development build is not signed, so its
// updates are never run by the app.
bool SignedBySameDeveloper(const fs::path& installer) {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    const std::string mine = ThumbprintOf(self);
    return !mine.empty() && mine == ThumbprintOf(installer);
}

// Starts the installer. /SILENT shows only its progress. /CLOSEAPPLICATIONS closes this app if it is still open, and
// /RESTARTAPPLICATIONS opens it again once the update is in place.
void RunInstaller(const fs::path& installer) {
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", installer.c_str(),
                                           L"/SILENT /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS", nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        throw std::runtime_error("the installer could not be started");
    }
}

fs::path UpdateFolder() {
    return platform::DataFolder() / L"Updates";
}

}  // namespace

void CheckInBackground(winrt::Microsoft::UI::Dispatching::DispatcherQueue queue,
                       std::function<void(updates::CheckResult)> done) {
    // The check waits on the network, so it runs on its own thread. The result is handed back to the UI thread.
    auto callback = std::make_shared<std::function<void(updates::CheckResult)>>(std::move(done));
    std::thread([queue, callback] {
        WinHttpTransport transport;
        updates::CheckResult result = updates::check(transport, updates::Platform::windows);
        queue.TryEnqueue([callback, result = std::move(result)]() mutable { (*callback)(std::move(result)); });
    }).detach();
}

void Offer(xaml::XamlRoot const& root, winrt::Microsoft::UI::Dispatching::DispatcherQueue queue,
           const updates::Release& release) {
    const std::string version = release.version.text();
    auto status = ui::Text("Kettle Logic " + version + " is available. Your recipes and settings are kept.");
    ctl::TextBlock progress = ui::Text("", ui::Kind::secondary);
    auto busy = std::make_shared<bool>(false);

    ui::DialogOptions options;
    options.title = "Update Available";
    options.primaryText = "Install Update";
    options.closeText = "Later";
    options.width = 460;
    options.content = ui::VStack({status, progress, ui::LinkButton("Release notes", [url = release.url] {
        platform::OpenUrl(url);
    })}, 12);

    options.primary = [queue, release, progress, busy] {
        if (*busy) return false;
        *busy = true;
        progress.Text(platform::H("Downloading the update…"));
        std::thread([queue, release, progress, busy] {
            try {
                const fs::path installer = Download(release, UpdateFolder());
                queue.TryEnqueue([progress, busy] { progress.Text(platform::H("Checking the update…")); });
                if (!SignedBySameDeveloper(installer)) {
                    throw std::runtime_error(
                        "The update is not signed by the same publisher as this copy of Kettle Logic, so it was not run. "
                        "Download it from the release page instead.");
                }
                queue.TryEnqueue([installer, progress, busy] {
                    progress.Text(platform::H("Installing. Kettle Logic will close and open again when it is done."));
                    RunInstaller(installer);
                    xaml::Application::Current().Exit();
                });
            } catch (const std::exception& error) {
                const std::string message = error.what();
                queue.TryEnqueue([progress, busy, message] {
                    progress.Text(platform::H(message));
                    *busy = false;
                });
            }
        }).detach();
        return false;
    };
    ui::ShowDialog(root, options);
}

}  // namespace kl::update_check
